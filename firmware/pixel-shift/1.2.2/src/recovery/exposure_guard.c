/* 自写私有 AE 接口适配。仅往返测试／同次开机内恢复，不触发曝光。
 * 不等于首帧锁存，不证明自动 ISO 锁定；不安装、不修改曝光档位。
 * 生产 ODIN 路径固定，编译时替换仅用于隔离模拟测试。 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef ODIN_PATH
#define ODIN_PATH "/system/bin/odindb-send"
#endif
#define LIMIT 512
#define CALL_TIMEOUT_MS 2000
static const char *props[]={"ael_reset_after_exposure","ael_user","ae_lock_enabled",
    "exp_mode","av_selected","tv_selected","sv_selected"};
typedef struct { char magic[8],boot[40]; int32_t original[7]; uint32_t phase,check; } Record;
_Static_assert(sizeof(Record)==84,"journal ABI");
static volatile sig_atomic_t interrupted;
static void stop(int s){(void)s;interrupted=1;}
static int64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static uint32_t checksum(const Record *r){
    const unsigned char *p=(const unsigned char*)r;uint32_t h=2166136261u;
    for(size_t i=0;i<sizeof(*r)-sizeof(r->check);i++)h=(h^p[i])*16777619u;
    return h;
}
static int boot_id(char out[40]){
    int fd=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    char b[40]={0};ssize_t n=read(fd,b,sizeof b);close(fd);
    if(n!=37 || b[36]!='\n')return 0;
    for(int i=0;i<36;i++)if(!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')||b[i]=='-'))return 0;
    memcpy(out,b,36);return 1;
}
/* 每次使用固定 argv，无 shell；输出上限及子进程截止时间。 */
static int call(const char *prop,const char *value,char out[LIMIT]){
    int fds[2];if(pipe2(fds,O_CLOEXEC))return 0;
    pid_t pid=fork();if(pid<0){close(fds[0]);close(fds[1]);return 0;}
    if(!pid){
        setpgid(0,0);close(fds[0]);dup2(fds[1],STDOUT_FILENO);dup2(fds[1],STDERR_FILENO);close(fds[1]);
        if(value)execl(ODIN_PATH,ODIN_PATH,"-s","camera","-p",prop,value,(char*)NULL);
        else execl(ODIN_PATH,ODIN_PATH,"-s","camera","-p",prop,(char*)NULL);
        _exit(127);
    }
    setpgid(pid,pid);close(fds[1]);fcntl(fds[0],F_SETFL,O_NONBLOCK);
    int status=0,finished=0,eof=0,ok=1;size_t used=0;int64_t until=now()+CALL_TIMEOUT_MS;
    while(!finished || !eof){
        char b[128];ssize_t n=read(fds[0],b,sizeof b);
        if(n>0){if(used+(size_t)n>=LIMIT){ok=0;break;}memcpy(out+used,b,(size_t)n);used+=(size_t)n;}
        else if(n==0)eof=1;
        else if(errno!=EAGAIN&&errno!=EINTR){ok=0;break;}
        if(!finished){pid_t w=waitpid(pid,&status,WNOHANG);if(w==pid)finished=1;else if(w<0&&errno!=EINTR){ok=0;break;}}
        if(finished&&eof)break;
        if(now()>=until){ok=0;break;}
        struct pollfd p={fds[0],POLLIN,0};poll(&p,1,10);
    }
    if(!ok){kill(-pid,SIGKILL);kill(pid,SIGKILL);}
    if(!finished)while(waitpid(pid,&status,0)<0&&errno==EINTR){}
    close(fds[0]);out[used]=0;
    return ok&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int get(const char *prop,int32_t *value){
    char b[LIMIT],prefix[80];if(!call(prop,NULL,b))return 0;
    int n=snprintf(prefix,sizeof prefix,"%s = ",prop);if(n<0||(size_t)n>=sizeof prefix||strncmp(b,prefix,(size_t)n))return 0;
    char *v=b+n;size_t len=strlen(v);if(len&&v[len-1]=='\n')v[--len]=0;if(len&&v[len-1]=='\r')v[--len]=0;
    if(!strcmp(prop,props[0])||!strcmp(prop,props[1])||!strcmp(prop,props[2])){
        if(!strcmp(v,"true"))*value=1;else if(!strcmp(v,"false"))*value=0;else return 0;return 1;
    }
    if(!strcmp(prop,"exposure_status")){if(strcmp(v,"E_ExposureStatus_None(0)"))return 0;*value=0;return 1;}
    if(!strcmp(prop,"exp_mode")){
        if(strncmp(v,"E_ExpMode_",10))return 0;
        char *p=strchr(v,'(');if(!p||p==v+10)return 0;
        for(char *q=v;q<p;q++)if(!((*q>='A'&&*q<='Z')||(*q>='a'&&*q<='z')||*q=='_'))return 0;
        v=p+1;len=strlen(v);if(!len||v[len-1]!=')')return 0;v[len-1]=0;
    }
    if(!*v)return 0;
    for(char *q=v;*q;q++)if(*q<'0'||*q>'9')return 0;
    errno=0;char *end;long parsed=strtol(v,&end,10);
    if(errno||*end||parsed<0||parsed>65535)return 0;
    *value=(int32_t)parsed;return 1;
}
static int idle(void){int32_t a,b;return get("exposure_status",&a)&&get("exposures_processing_counter",&b)&&a==0&&b==0;}
static int set_bool(unsigned i,int32_t value){
    if(i>1||(value!=0&&value!=1))return 0;
    char b[LIMIT];int32_t actual;
    return call(props[i],value?"true":"false",b)&&!b[0]&&get(props[i],&actual)&&actual==value;
}
static int save(int fd,Record *r){
    r->check=checksum(r);const unsigned char *p=(const unsigned char*)r;size_t done=0;
    while(done<sizeof *r){ssize_t n=pwrite(fd,p+done,sizeof(*r)-done,(off_t)done);if(n<0&&errno==EINTR)continue;if(n<=0)return 0;done+=(size_t)n;}
    return !ftruncate(fd,sizeof *r)&&!fsync(fd);
}
static int parent_sync(const char *path){
    char b[4096];if(strlen(path)>=sizeof b)return 0;strcpy(b,path);
    char *p=strrchr(b,'/');if(!p)return 0;if(p==b)p[1]=0;else *p=0;
    int fd=open(b,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;int ok=!fsync(fd);close(fd);return ok;
}
static int snapshot(Record *r){
    memset(r,0,sizeof *r);memcpy(r->magic,"X2DAEG1",8);r->phase=1;
    if(!boot_id(r->boot)){fprintf(stderr,"AE_SNAPSHOT_FAILED boot_id errno=%d\n",errno);return 0;}
    if(!idle()){fputs("AE_SNAPSHOT_FAILED idle_before\n",stderr);return 0;}
    for(unsigned i=0;i<7;i++)if(!get(props[i],&r->original[i])){
        fprintf(stderr,"AE_SNAPSHOT_FAILED %s\n",props[i]);return 0;
    }
    if(!idle()){fputs("AE_SNAPSHOT_FAILED idle_after\n",stderr);return 0;}
    return 1;
}
static int same_selection(const Record *r){
    int32_t v;for(unsigned i=3;i<7;i++)if(!get(props[i],&v)||v!=r->original[i])return 0;return 1;
}
static int restore(int fd,Record *r){
    /* 不在拍摄中解锁，也不覆盖用户改变的档位／曝光值。保留日志等待处理。 */
    if(!idle()||!same_selection(r))return 0;
    int ok=1;for(int i=1;i>=0;i--){int32_t v;if(!get(props[i],&v)||(v!=r->original[i]&&!set_bool((unsigned)i,r->original[i])))ok=0;}
    int32_t v;for(unsigned i=0;i<7;i++)if(!get(props[i],&v)||v!=r->original[i])ok=0;
    if(!ok)return 0;
    r->phase=2;return save(fd,r);
}
#ifndef HOLD_MS
#define HOLD_MS 60000
#endif
int main(int argc,char **argv){
    if(argc!=3 || (strcmp(argv[1],"--roundtrip")&&strcmp(argv[1],"--recover")&&strcmp(argv[1],"--hold-60"))){
        fprintf(stderr,"usage: exposure-guard --roundtrip|--recover|--hold-60 /private/job/ae.journal\n");return 2;
    }
    struct sigaction sa={0};sa.sa_handler=stop;sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
    int recovery=!strcmp(argv[1],"--recover");
    if(argv[2][0]!='/'||strstr(argv[2],"/../")){fprintf(stderr,"absolute private journal required\n");return 2;}
    int fd=open(argv[2],O_RDWR|O_CLOEXEC|O_NOFOLLOW|(recovery?0:O_CREAT|O_EXCL),0600);
    if(fd<0){perror("journal open");return 2;}
    struct stat st;int owned=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==geteuid();
    if(!owned||flock(fd,LOCK_EX|LOCK_NB)){fprintf(stderr,"journal ownership or concurrent job\n");close(fd);return 2;}
    Record r;int result=1;
    if(recovery){
        char boot[40]={0};
        if(st.st_size!=sizeof r||pread(fd,&r,sizeof r,0)!=sizeof r||memcmp(r.magic,"X2DAEG1",8)||r.check!=checksum(&r)||
           (r.phase!=1&&r.phase!=2)||!boot_id(boot)||memcmp(boot,r.boot,sizeof boot)){
            fprintf(stderr,"invalid journal or different boot; no settings written\n");goto finish;
        }
        if(r.original[0]<0||r.original[0]>1||r.original[1]<0||r.original[1]>1||r.original[2]<0||r.original[2]>1)goto finish;
        if(r.phase==2){puts("ALREADY_RESTORED");result=0;goto finish;}
        result=restore(fd,&r)?0:4;puts(result?"RESTORE_PENDING":"RECOVERED");goto finish;
    }
    if(!snapshot(&r)||interrupted||!save(fd,&r)||!parent_sync(argv[2])){
        fprintf(stderr,"preflight/journal failed; no settings written\n");goto finish;
    }
    /* 原值先持久化。任一 setter 回包丢失也进入恢复，不假定未生效。 */
    int32_t enabled=0;
    int locked=!interrupted&&set_bool(0,0)&&!interrupted&&set_bool(1,1)&&get(props[2],&enabled)&&enabled==1&&same_selection(&r);
    if(locked){
        puts("AE_LOCK_CONFIRMED_NOT_SIX_FRAME_LATCH");fflush(stdout);
        if(!strcmp(argv[1],"--hold-60")){
            int64_t until=now()+HOLD_MS;
            while(!interrupted&&now()<until){struct timespec nap={0,50000000};nanosleep(&nap,NULL);}
        }
    }
    int was_interrupted=interrupted;interrupted=0;
    if(!restore(fd,&r)){puts("RESTORE_PENDING");result=4;}
    else {puts("RESTORED");result=locked&&!was_interrupted?0:1;}
finish:
    close(fd);return result;
}
