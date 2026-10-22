/* 自写 X2D II 1.3.16.2 临时按键桥。修改自进程虚表/GOT，候选另含
 * 快速许可函数 RAM 指令跳转；指纹不符拒绝，不改磁盘、不调用曝光。
 * 全按路由及忙碌阶段半按隔离；其他设置键由界面处理。 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <link.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <time.h>
#include "shutter_gate.h"
#include "shutter_fingerprint.h"
#include "capture_packet.h"
#include "request_publish.h"
#define ROOT "/dev/x2d2-shutter-v1"
typedef void (*KeyFn)(void *,const void *);
static uintptr_t base;static KeyFn original;static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
typedef ssize_t (*ReadFn)(int,void *,size_t,size_t);
static ReadFn original_read;
static unsigned long capture_packets;
static unsigned gate_read_reason;static unsigned long gate_links;
static Gate gate;static PsShutterPolicy policy;static unsigned long passed,swallowed,queued;
static Gate pending;static int have_pending,failed,diagnostic;static unsigned long full_down,full_up;static _Atomic int installed;
typedef struct {uint64_t time_ms;unsigned type,key;int down;} KeySample;
static KeySample samples[64];static unsigned sample_count,sample_sent;
/* 只记录两个 CaptureService 读取点的短消息，不记录照片或其他 fd 数据。 */
typedef struct {uint64_t time_ms;uintptr_t caller;long received;unsigned char bytes[12];} PacketSample;
static PacketSample packet_samples[64];static unsigned packet_count,packet_sent;
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
#ifdef PS_FAST_VIRTUAL
#include "fast_capture_virtual.inc"
#else
#include "fast_capture_runtime.inc"
#endif
static int read_gate(Gate *g){
 gate_read_reason=0;gate_links=999;
 int fd=open(ROOT"/gate",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;if(fd<0){gate_read_reason=1;return 0;}
 if(fstat(fd,&st)){gate_read_reason=2;close(fd);return 0;}
 gate_links=(unsigned long)st.st_nlink;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==sizeof *g&&read(fd,g,sizeof *g)==sizeof *g;close(fd);
 if(!ok){gate_read_reason=3;return 0;}
 if(g->magic!=UINT64_C(0x315447485350)||g->mode>PS_GATE_FAILED||g->ready>1){gate_read_reason=4;return 0;}
 return 1;
}
static int write_file(const char *path,const void *data,size_t size,int exclusive){
 int fd=open(path,O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW|(exclusive?O_EXCL:0),0600);if(fd<0)return 0;
 struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==0&&
  !ftruncate(fd,0)&&write(fd,data,size)==(ssize_t)size&&!fsync(fd);close(fd);return ok;
}
static int decide(unsigned key,int down,unsigned type,int capture){
 /* 临界区只有普通内存，不在原厂输入线程读盘或等待后台。 */
 pthread_mutex_lock(&mutex);
 if(capture)capture_packets++;
 if(diagnostic&&sample_count<64){
  samples[sample_count++]=(KeySample){now(),type,key,down};}
 if(key==0x45){if(down)full_down++;else full_up++;}
 uint64_t tick=now();if(gate.mode!=PS_GATE_OFF&&(!tick||gate.deadline_ms<tick))ps_gate_fail(&gate,&failed);
 int route_ready=ps_fast_blocked(&fast_guard,gate.mode,gate.session,gate.sequence);
 int result=ps_key_event(&policy,key,down,gate.mode,gate.deadline_ms>=tick,gate.ready&&route_ready,gate.sequence);
 if(result==PS_KEY_QUEUE){if(!have_pending){pending=gate;have_pending=1;queued++;}gate.ready=0;}
 if(result==PS_KEY_PASS)passed++;else swallowed++;
 pthread_mutex_unlock(&mutex);
 return result;
}
static void handle(void *self,const void *event){
 unsigned key=0,type=0;memcpy(&key,(const char*)event+0x1c,4);memcpy(&type,(const char*)event+0x18,4);
 int down=*((const unsigned char*)event+0x20)!=0;
 int result=decide(key,down,type,0);
 if(result==PS_KEY_PASS)original(self,event);
}
/* 不吞掉读操作或消息 ACK。候选方案将扩展持有的全按转换为松开，
 * 原厂 onReadData 仍执行原有 ACK。是否可安全替代全按必须另做真机验收。 */
static ssize_t capture_read(int fd,void *data,size_t size,size_t capacity){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0)-base;
 ssize_t n=original_read(fd,data,size,capacity);
 fast_read_observe(caller,data,size,capacity,n);
 if((caller==0x23a8e4||caller==0x23a9e8)&&size==12&&capacity==12&&n>=2&&n<=12&&data&&((unsigned char*)data)[1]==5){
  pthread_mutex_lock(&mutex);
  if(packet_count<64){
   PacketSample *s=&packet_samples[packet_count++];s->time_ms=now();s->caller=caller;s->received=n;
   memcpy(s->bytes,data,(size_t)n);
  }
  pthread_mutex_unlock(&mutex);
 }
 if(ps_capture_full_packet(caller,size,capacity,n,data)){
  unsigned char *p=data;int action=decide(0x45,p[4]!=0,0x10005,1);
  if(action!=PS_KEY_PASS)p[4]=0;
 }
 return n;
}
static void *publish(void *unused){
 (void)unused;unsigned long heartbeat=0;
 for(;;){
  Gate next={0};int valid=read_gate(&next);Gate request={0};int send=0;char line[1024];KeySample batch[64];unsigned count=0;
  PacketSample packets[64];unsigned packets_n=0;
  uint64_t gate_time=now();
  pthread_mutex_lock(&mutex);
  if(diagnostic){gate.mode=PS_GATE_DIAGNOSTIC;gate.ready=0;gate.deadline_ms=now()+2000;}
  else ps_gate_update(&gate,&policy,&failed,&next,valid,gate_time);
  if(have_pending){request=pending;have_pending=0;send=1;}
  while(sample_sent<sample_count)batch[count++]=samples[sample_sent++];
  while(packet_sent<packet_count)packets[packets_n++]=packet_samples[packet_sent++];
  int n=snprintf(line,sizeof line,"{\"installed\":%s,\"heartbeat\":%lu,\"mode\":%u,\"passed\":%lu,\"swallowed\":%lu,\"queued\":%lu,\"full_down\":%lu,\"full_up\":%lu,\"capture_packets\":%lu,\"gate_valid\":%d,\"gate_reason\":%u,\"gate_links\":%lu,\"input_mode\":%u,\"deadline_delta\":%lld,\"failed\":%d}\n",atomic_load(&installed)?"true":"false",++heartbeat,gate.mode,passed,swallowed,queued,full_down,full_up,capture_packets,valid,gate_read_reason,gate_links,next.mode,(long long)next.deadline_ms-(long long)gate_time,failed);
  if(n>1&&(size_t)n<sizeof line){
   int extra=snprintf(line+n-2,sizeof line-(size_t)n+2,",\"fast_guard_installed\":%s,\"fast_checks\":%lu,\"fast_ack\":%d,\"fast_pending\":%u,\"fast_fault\":%d,\"fast_session\":%llu,\"fast_sequence\":%llu,\"fast_blocked\":%s}\n",
    fast_patched&&fast_write_patched?"true":"false",fast_checks,fast_guard.acknowledged,fast_guard.count,fast_guard.fault,
    (unsigned long long)fast_guard.seen_session,(unsigned long long)fast_guard.seen_sequence,
    ps_fast_blocked(&fast_guard,gate.mode,gate.session,gate.sequence)?"true":"false");
   n=extra>0?n-2+extra:-1;
  }
#ifdef PS_FAST_REFRESH
  if(n>1&&(size_t)n<sizeof line){
   int extra=snprintf(line+n-2,sizeof line-(size_t)n+2,",\"fast_timer_id\":%d,\"fast_timer_ticks\":%lu,\"fast_refresh_calls\":%lu}\n",fast_timer_id,fast_timer_ticks,fast_refresh_calls);
   n=extra>0?n-2+extra:-1;
  }
#endif
#ifdef PS_FAST_DIAGNOSTICS
  if(n>1&&(size_t)n<sizeof line){
   int extra=snprintf(line+n-2,sizeof line-(size_t)n+2,",\"native_cached_before\":%u,\"native_cached_after\":%u,\"native_stop_down\":%u,\"target_writes\":%lu,\"ready_writes\":%lu,\"target_reads\":%lu,\"ack_reads\":%lu}\n",fast_cached_before,fast_cached_after,fast_original_value,fast_target_writes,fast_ready_writes,fast_target_reads,fast_ack_reads);
   n=extra>0?n-2+extra:-1;
  }
#endif
#ifdef PS_FAST_RECONFIRM
  if(n>1&&(size_t)n<sizeof line){
   int extra=snprintf(line+n-2,sizeof line-(size_t)n+2,",\"reconfirm_attempts\":%u,\"reconfirm_returned\":%u,\"reconfirm_rejected\":%u}\n",fast_reconfirm_attempts,fast_reconfirm_returned,fast_reconfirm_rejected);
   n=extra>0?n-2+extra:-1;
  }
#endif
  pthread_mutex_unlock(&mutex);
  if(packets_n){
   int fd=open(ROOT"/packet-audit.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
   if(fd>=0){
    struct stat st;
    if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size<16384){
     for(unsigned i=0;i<packets_n;i++){
      char hex[25];unsigned len=(unsigned)packets[i].received;
      for(unsigned j=0;j<len;j++)snprintf(hex+j*2,3,"%02x",packets[i].bytes[j]);
      dprintf(fd,"t=%llu caller=%lx bytes=%u raw=%s\n",(unsigned long long)packets[i].time_ms,(unsigned long)packets[i].caller,len,hex);
     }
     fsync(fd);
    }
    close(fd);
   }
  }
  for(unsigned i=0;i<count;i++)fprintf(stderr,"PS_KEY_EVENT t=%llu type=%u key=0x%x down=%d\n",
   (unsigned long long)batch[i].time_ms,batch[i].type,batch[i].key,batch[i].down);
  /* request 不覆盖：后台领取前的重按不会产生第二次请求。 */
  if(send&&!ps_publish_request(ROOT"/request.next",ROOT"/request",&request,sizeof request)){
   pthread_mutex_lock(&mutex);ps_gate_fail(&gate,&failed);pthread_mutex_unlock(&mutex);
  }
  if(n>0&&(size_t)n<sizeof line&&write_file(ROOT"/status.next",line,n,0))rename(ROOT"/status.next",ROOT"/status.json");
  struct timespec t={0,100000000};nanosleep(&t,NULL);
 }
 return NULL;
}
static int image(struct dl_phdr_info *i,size_t size,void *unused){
 (void)size;(void)unused;
 if(!i->dlpi_name||!i->dlpi_name[0]||!strcmp(i->dlpi_name,"/system/bin/camera-service"))base=i->dlpi_addr;
 return 0;
}
__attribute__((constructor))static void init(void){
 char exe[256];ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1);if(n<=0)return;exe[n]=0;
 if(strcmp(exe,"/system/bin/camera-service")||getuid()!=0)return;
 const char *diag=getenv("PS_SHUTTER_DIAGNOSTIC");diagnostic=diag&&!strcmp(diag,"1");
 if(diagnostic){gate.mode=PS_GATE_DIAGNOSTIC;gate.deadline_ms=now()+2000;}
 fputs("PS_SHUTTER_LIBRARY_LOADED\n",stderr);
 dl_iterate_phdr(image,NULL);if(!base){fputs("PS_SHUTTER_NO_BASE\n",stderr);return;}
 for(unsigned i=0;i<sizeof ps_checks/sizeof ps_checks[0];i++)
  if(memcmp((void*)(base+ps_checks[i].offset),ps_checks[i].bytes,ps_checks[i].length)){fprintf(stderr,"PS_SHUTTER_BAD_FINGERPRINT %u\n",i);return;}
 uintptr_t *slot=(uintptr_t*)(base+PS_KEY_SLOT),expected=base+PS_KEY_ORIGINAL;
 if(*slot!=expected){fputs("PS_SHUTTER_BAD_SLOT\n",stderr);return;}
 long pagesize=sysconf(_SC_PAGESIZE);if(pagesize!=4096)return;
 uintptr_t *read_slot=(uintptr_t*)(base+PS_CAPTURE_READ_SLOT);
 original_read=(ReadFn)dlsym(RTLD_DEFAULT,"__read_chk");
 if(!original_read||*read_slot!=(uintptr_t)original_read){fputs("PS_SHUTTER_BAD_READ_SLOT\n",stderr);return;}
 uintptr_t read_page=(uintptr_t)read_slot&~(uintptr_t)4095;
 uintptr_t page=(uintptr_t)slot&~(uintptr_t)4095;original=(KeyFn)expected;
 if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){perror("PS_SHUTTER_MPROTECT");return;}
 __atomic_store_n(slot,(uintptr_t)handle,__ATOMIC_RELEASE);
 if(mprotect((void*)page,4096,PROT_READ)){
  __atomic_store_n(slot,expected,__ATOMIC_RELEASE);mprotect((void*)page,4096,PROT_READ);return;}
 if(mprotect((void*)read_page,4096,PROT_READ|PROT_WRITE)){
  if(!mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){
   __atomic_store_n(slot,expected,__ATOMIC_RELEASE);mprotect((void*)page,4096,PROT_READ);}return;}
 __atomic_store_n(read_slot,(uintptr_t)capture_read,__ATOMIC_RELEASE);
 if(mprotect((void*)read_page,4096,PROT_READ)){
  __atomic_store_n(read_slot,(uintptr_t)original_read,__ATOMIC_RELEASE);mprotect((void*)read_page,4096,PROT_READ);
  if(!mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){
   __atomic_store_n(slot,expected,__ATOMIC_RELEASE);mprotect((void*)page,4096,PROT_READ);}return;}
 int fast_ok=fast_install();
 if(!fast_ok)fputs("PS_FAST_GUARD_UNAVAILABLE_NO_ARM\n",stderr);
 pthread_t thread;atomic_store(&installed,1);
 if(pthread_create(&thread,NULL,publish,NULL)){
  if(!fast_restore())_exit(125);
  atomic_store(&installed,0);if(!mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){
   __atomic_store_n(slot,expected,__ATOMIC_RELEASE);mprotect((void*)page,4096,PROT_READ);}
  if(!mprotect((void*)read_page,4096,PROT_READ|PROT_WRITE)){
   __atomic_store_n(read_slot,(uintptr_t)original_read,__ATOMIC_RELEASE);mprotect((void*)read_page,4096,PROT_READ);}return;}
 pthread_detach(thread);fputs("PS_SHUTTER_INSTALLED_PASS_THROUGH\n",stderr);
}
