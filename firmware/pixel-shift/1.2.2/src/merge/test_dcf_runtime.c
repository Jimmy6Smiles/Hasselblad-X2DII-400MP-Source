#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include "dcf_sync_contract.h"
typedef struct {void *owner;const uint16_t *chars;int64_t length;} PsQStringView;
static uintptr_t base;
static unsigned char service[256] __attribute__((aligned(16))),dm[128] __attribute__((aligned(16))),dc[128] __attribute__((aligned(16))),di[128] __attribute__((aligned(16))),cv[128] __attribute__((aligned(16))),sv[128] __attribute__((aligned(16)));
static int global=2719,next=2720,persistent=2719,writes,wrong_thread,busy,wrong_file;
static void *owner(const void *p){(void)p;return (void*)(uintptr_t)(wrong_thread?2:1);}
static void *current(void){return (void*)1;}
static void *(*album_object_thread)(const void*)=owner;
static void *(*album_current_thread)(void)=current;
static int processing(void *p){assert(p==service);return busy;}
static int gindex(void *p){assert(p==dm);return global;}
static int pindex(void *p){assert(p==di);return persistent;}
static int vindex(void *p){assert(p==cv);return next-1;}
static int folderindex(void *p){assert(p==cv);return 999;}
static void setv(void *p,int n){assert(p==cv&&writes==0);next=n;writes++;}
static void setg(void *p,int n){assert(p==dc&&writes==1);global=n;writes++;}
static void setp(void *p,int n){assert(p==di&&writes==2);persistent=n;writes++;}
static const PsQStringView *vpath(void *p){
 static uint16_t c[17],s[17];static PsQStringView a={0,c,17},b={0,s,17};
 for(int i=0;i<17;i++){c[i]="/mnt/media_rw/cfe"[i];s[i]="/mnt/media_rw/ssd"[i];}
 return p==cv?&a:&b;
}
static int album_path(const PsQStringView *p,char out[14]){
 if(p->length!=13)return 0;for(int i=0;i<13;i++)out[i]=(char)p->chars[i];out[13]=0;return ps_album_folder(out);
}
static int fake_fstat(int fd,struct stat *s){int r=fstat(fd,s);if(!r)s->st_uid=0;return r;}
static long fake_rename(long nr,int a,const char *from,int b,const char *to,int flags){
 assert(nr==SYS_renameat2&&a==AT_FDCWD&&b==AT_FDCWD&&flags==1);
 struct stat st;if(!lstat(to,&st))return -1;return rename(from,to);
}
static int fake_lstat(const char *p,struct stat *s){
 if(!strcmp(p,"/mnt/media_rw/cfe/DCIM/999HASBL/B0002720.3FR")){
  memset(s,0,sizeof *s);s->st_mode=S_IFREG|0600;s->st_nlink=1;s->st_dev=1;s->st_ino=wrong_file?3:2;s->st_size=816320512;return 0;
 }return lstat(p,s);
}
#define PS_DCF_HOST_TEST
#define PS_ALBUM_SERVICE_VTABLE 0x100
#define DCF_MAIN_VT 0x200
#define DCF_VT 0x300
#define DCF_INDEX_VT 0x400
#define DCF_VOLUME_VT 0x500
#define DCF_PROCESSING ((uintptr_t)processing)
#define DCF_MAIN_INDEX ((uintptr_t)gindex)
#define DCF_INDEX_GET ((uintptr_t)pindex)
#define DCF_VOLUME_INDEX ((uintptr_t)vindex)
#define DCF_VOLUME_FOLDER ((uintptr_t)folderindex)
#define DCF_VOLUME_PATH ((uintptr_t)vpath)
#define DCF_VOLUME_SET ((uintptr_t)setv)
#define DCF_GLOBAL_SET ((uintptr_t)setg)
#define DCF_INDEX_SET ((uintptr_t)setp)
static const struct {uintptr_t offset;unsigned length;unsigned char bytes[32];} dcf_checks[]={{(uintptr_t)service,0,{0}}};
#define fstat fake_fstat
#define lstat fake_lstat
#define syscall fake_rename
#include "dcf_sync_runtime.inc"
#undef fstat
#undef lstat
#undef syscall
int main(void){
 *(uintptr_t*)service=0x100;*(uintptr_t*)dm=0x200;*(uintptr_t*)dc=0x300;*(uintptr_t*)di=0x400;*(uintptr_t*)cv=*(uintptr_t*)sv=0x500;
 *(void**)(service+0xf0)=dm;*(void**)(dm+0x20)=dc;*(void**)(dm+0x40)=di;*(void**)(dc+0x40)=sv;*(void**)(dc+0x48)=cv;
 uint16_t chars[13];for(int i=0;i<13;i++)chars[i]="/cfe/999HASBL"[i];PsQStringView path={0,chars,13};
 struct timespec ts;assert(!clock_gettime(CLOCK_MONOTONIC,&ts));
 PsDcfSync r={.magic=PS_DCF_MAGIC,.token=1,.deadline_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000+60000,.device=1,.inode=2,.bytes=816320512,.expected=2719,.target=2720,.folder="/cfe/999HASBL"};
 int fd=open(PS_ALBUM_REFRESH_ROOT"/dcf.request",O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);assert(write(fd,&r,sizeof r)==sizeof r);close(fd);
 wrong_thread=1;dcf_before(service,&path,0);assert(!writes);wrong_thread=0;
 busy=1;dcf_before(service,&path,0);assert(!writes);busy=0;
 wrong_file=1;dcf_before(service,&path,0);assert(!writes);wrong_file=0;
 global=2721;dcf_before(service,&path,0);assert(!writes);global=2719;
 dcf_before(service,&path,0);assert(writes==3&&global==2720&&persistent==2720&&next==2721);
 dcf_before(service,&path,0);assert(writes==3);
 assert(!access(PS_ALBUM_REFRESH_ROOT"/dcf-0000000000000001.result",F_OK));
 puts("DCF_RUNTIME_REQUEST_IDENTITY_THREAD_BUSY_COLLISION_REPLAY_PASS_MOCK_ABI");
}
