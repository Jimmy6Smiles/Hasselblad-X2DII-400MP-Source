/* Passive signal observer for exactly X2DII 1.3.16.2 camera-storage.
 * No READY generation, camera/property calls, image reads, deletion or recovery.
 * Fixed-size nonblocking pipe decouples Qt signal delivery from log writes.
 * Intended to link alongside existing album observer, not replace it standalone. */
#define _GNU_SOURCE
#include "flush_event.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <link.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/stat.h>
#include "stream_wire.h"
static int stream_fd=-1;
static uint64_t stream_sequence;
typedef void (*Activate)(void*,const void*,int,void**);
static Activate forward;
static uintptr_t image_base;
static const void *cache_meta,*fileio_meta;
static int channel_fd[2]={-1,-1},log_fd=-1;
static _Atomic unsigned dropped,seen,invalid;
static _Atomic int hook_state;
static uint64_t now_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void stream_emit(const FlushEvent *e){
    StreamPacket p={.magic=PS_STREAM_MAGIC,.sequence=++stream_sequence,.stamp=now_ns()};
    p.dropped=atomic_load(&dropped);p.hook=atomic_load(&hook_state);
    if(e)p.event=*e;
    if(stream_fd>=0)(void)write(stream_fd,&p,sizeof p);
}
static void observe_signal(void *sender,const void *meta,int signal,void **args){
    int saved_errno=errno;
    int source=(meta==cache_meta&&signal==3)?1:((fileio_meta&&meta==fileio_meta&&signal==0)?2:0);
    if(source){
        atomic_fetch_add(&seen,1);
        FlushEvent e={.monotonic_ns=now_ns(),.source=source,.work_kind=UINT32_MAX,.error_code=256};
        const FlushQString *q=source==2?flush_result_view(args?args[1]:NULL,&e):(args?(const FlushQString*)args[2]:NULL);
        e.canonical=flush_path(q,&e);
        if(!e.canonical){
            atomic_fetch_add(&invalid,1);
            /* Diagnostic text only, never fed to READY or used as a path. */
            if(q&&q->chars&&q->length>0&&q->length<64){
                for(int64_t i=0;i<q->length;i++)e.path[i]=(q->chars[i]>=32&&q->chars[i]<127)?(char)q->chars[i]:'?';
            }
        }
        if(q&&write(channel_fd[1],&e,sizeof e)!=(ssize_t)sizeof e)atomic_fetch_add(&dropped,1);
    }
    errno=saved_errno;
    forward(sender,meta,signal,args);
}
static void *log_events(void *unused){
    (void)unused;uint64_t deadline=now_ns()+UINT64_C(1800000000000);
    unsigned recorded=0,last_seen=~0u;int last_hook=-99;
    for(;;){
        stream_emit(NULL);
        int logging=log_fd>=0&&now_ns()<deadline&&recorded<128;
        unsigned current=atomic_load(&seen);int installed=atomic_load(&hook_state);
        if(logging&&(current!=last_seen||installed!=last_hook)){
            dprintf(log_fd,"OBSERVER_STATUS hook=%d seen=%u invalid=%u dropped=%u NOT_READY\n",installed,current,atomic_load(&invalid),atomic_load(&dropped));
            last_seen=current;last_hook=installed;
        }
        struct pollfd p={channel_fd[0],POLLIN,0};int rc=poll(&p,1,200);
        if(rc<0&&errno!=EINTR)break;
        if(rc<=0)continue;
        FlushEvent e;ssize_t n=read(channel_fd[0],&e,sizeof e);
        if(n==-1&&(errno==EINTR||errno==EAGAIN))continue;
        if(n!=(ssize_t)sizeof e)break;
        stream_emit(&e);
        if(!logging)continue;
        if(!recorded)deadline=now_ns()+UINT64_C(300000000000);
        if(dprintf(log_fd,"FLUSH_OBSERVED ns=%llu source=%d kind=%u error=%u canonical=%d text=%s NOT_READY\n",(unsigned long long)e.monotonic_ns,e.source,e.work_kind,e.error_code,e.canonical,e.path)<0)break;
        recorded++;
    }
    dprintf(log_fd,"OBSERVATION_END recorded=%u seen=%u invalid=%u dropped=%u\n",recorded,atomic_load(&seen),atomic_load(&invalid),atomic_load(&dropped));
    /* Keep pipe descriptors valid until process exit; a full pipe only drops
     * observations and never blocks or changes normal Qt signal handling. */
    close(log_fd);log_fd=-1;return NULL;
}
#ifndef PS_FLUSH_HOST_TEST
static int locate(struct dl_phdr_info *i,size_t n,void *arg){
    (void)n;(void)arg;
    if(!i->dlpi_name||!i->dlpi_name[0]||!strcmp(i->dlpi_name,"/system/bin/camera-storage"))image_base=i->dlpi_addr;
    return 0;
}
__attribute__((constructor))static void install_observer(void){
    char exe[128];ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1);
    if(n<=0||n>=(ssize_t)sizeof exe-1)return;
    exe[n]=0;
    if(strcmp(exe,"/system/bin/camera-storage")||sysconf(_SC_PAGESIZE)!=4096)return;
    dl_iterate_phdr(locate,NULL);if(!image_base)return;
    const uint32_t fingerprint[]={0xd10103ff,0xa9027bfd,0x910083fd,0xf9001bf3,0xd53bd053,0x910003e3};
    if(memcmp((void*)(image_base+0x5af78),fingerprint,sizeof fingerprint))return;
    const uint32_t io_fingerprint[]={0xd10103ff,0xa9027bfd,0x910083fd,0xf9001bf3,0xd53bd053,0x910023e3};
    if(memcmp((void*)(image_base+0x10a4a0),io_fingerprint,sizeof io_fingerprint))return;
    const uint32_t path_fingerprint[]={0x91008000,0xd65f03c0};
    if(memcmp((void*)(image_base+0x11b260),path_fingerprint,sizeof path_fingerprint))return;
    uintptr_t *slot=(void*)(image_base+0x299720);
    forward=(Activate)dlsym(RTLD_DEFAULT,"_ZN11QMetaObject8activateEP7QObjectPKS_iPPv");
    if(!forward||*slot!=(uintptr_t)forward)return;
    cache_meta=*(const void**)(image_base+0x2978e0);if(!cache_meta)return;
    fileio_meta=*(const void**)(image_base+0x298660);
    if(fileio_meta!=(const void*)(image_base+0x285f08))return;
    log_fd=open("/dev/x2d2-album-refresh-v1/flush-observer.log",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(log_fd<0)return;
    struct stat fifo_st;
    if(mkfifo(PS_STREAM_FIFO,0600)&&errno!=EEXIST){close(log_fd);return;}
    stream_fd=open(PS_STREAM_FIFO,O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(stream_fd<0||fstat(stream_fd,&fifo_st)||!S_ISFIFO(fifo_st.st_mode)||fifo_st.st_uid!=geteuid()){
        if(stream_fd>=0)close(stream_fd);close(log_fd);return;
    }
    if(pipe2(channel_fd,O_CLOEXEC|O_NONBLOCK)){dprintf(log_fd,"OBSERVER_PIPE_FAILED errno=%d\n",errno);close(log_fd);log_fd=-1;return;}
    pthread_t thread;
    int thread_rc=pthread_create(&thread,NULL,log_events,NULL);
    if(thread_rc){dprintf(log_fd,"OBSERVER_THREAD_FAILED rc=%d\n",thread_rc);close(channel_fd[0]);close(channel_fd[1]);close(log_fd);return;}
    pthread_detach(thread);
    uintptr_t page=(uintptr_t)slot&~(uintptr_t)4095;
    if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){atomic_store(&hook_state,-1);return;}
    __atomic_store_n(slot,(uintptr_t)observe_signal,__ATOMIC_RELEASE);
    if(mprotect((void*)page,4096,PROT_READ)){
        __atomic_store_n(slot,(uintptr_t)forward,__ATOMIC_RELEASE);
        if(mprotect((void*)page,4096,PROT_READ))_exit(125);
        atomic_store(&hook_state,-2);
        return;
    }
    atomic_store(&hook_state,1);
    fputs("PS_FLUSH_OBSERVER_INSTALLED WAIT_1800_CAPTURE_300_SECONDS\n",stderr);
}
#endif
