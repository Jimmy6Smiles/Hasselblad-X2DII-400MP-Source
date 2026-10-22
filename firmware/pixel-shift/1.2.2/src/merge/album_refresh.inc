/* 仅固定固件、原厂 DBus 所属线程内触发原生重新扫描。
 * 唯一对象写入是 CacheDir 的已浏览标志；不替换照片、不伪造相册项。 */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <dlfcn.h>
#include "album_refresh_contract.h"
#ifndef PS_ALBUM_REFRESH_ROOT
#define PS_ALBUM_REFRESH_ROOT "/dev/x2d2-album-refresh-v1"
#endif
typedef struct {void *object,*ref;} PsAlbumShared;
extern void ps_album_lookup(uintptr_t,void*,const void*,PsAlbumShared*);
static void *(*album_object_thread)(const void*);
static void *(*album_current_thread)(void);
static int refresh_initialize(void){
 album_object_thread=dlsym(RTLD_DEFAULT,"_ZNK7QObject6threadEv");
 album_current_thread=dlsym(RTLD_DEFAULT,"_ZN7QThread13currentThreadEv");
 return album_object_thread&&album_current_thread;
}
static void refresh_before(void *self,const void *path,int options){
 char folder[14];if(options!=0||!album_path(path,folder))return;
 PsAlbumRefresh request;struct stat st;
 int fd=open(PS_ALBUM_REFRESH_ROOT"/refresh.request",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
  st.st_size==sizeof request&&read(fd,&request,sizeof request)==sizeof request;close(fd);
 struct timespec time;if(!ok||clock_gettime(CLOCK_MONOTONIC,&time))return;
 uint64_t tick=(uint64_t)time.tv_sec*1000+time.tv_nsec/1000000;
 if(!ps_album_refresh_valid(&request,folder,tick))return;
 if(*(uintptr_t*)self!=base+PS_ALBUM_SERVICE_VTABLE)return;
 void *cache=*(void**)((char*)self+PS_ALBUM_CACHE_MEMBER);
 if(!cache||*(uintptr_t*)cache!=base+PS_ALBUM_CACHE_VTABLE)return;
 void *thread=album_current_thread();
 if(!thread||album_object_thread(self)!=thread||album_object_thread(cache)!=thread)return;
 char physical[64];int n=snprintf(physical,sizeof physical,"/mnt/media_rw/%.3s/DCIM/%.8s",folder+1,folder+5);
 if(n<=0||n>=64)return;
 uint16_t chars[64];for(int i=0;i<n;i++)chars[i]=(unsigned char)physical[i];
 PsQStringView name={NULL,chars,n};PsAlbumShared item={0};
 ps_album_lookup(base+PS_ALBUM_LOOKUP,cache,&name,&item);
 if(item.object&&item.ref){
  unsigned char *dir=item.object;
  if(ps_album_refresh_allowed(1,*(uintptr_t*)dir==base+PS_ALBUM_DIR_VTABLE,
    *((unsigned char*)cache+PS_ALBUM_ENABLED),dir[PS_ALBUM_REMOVED],dir[PS_ALBUM_BROWSED])){
   char claim[sizeof(PS_ALBUM_REFRESH_ROOT)+64],result[sizeof(PS_ALBUM_REFRESH_ROOT)+64];
   snprintf(claim,sizeof claim,PS_ALBUM_REFRESH_ROOT"/request-%016llx.claimed",(unsigned long long)request.token);
   snprintf(result,sizeof result,PS_ALBUM_REFRESH_ROOT"/request-%016llx.result",(unsigned long long)request.token);
   /* 不覆盖旧令牌的证据，同线程中先领取再改变缓存状态。 */
   if(!syscall(SYS_renameat2,AT_FDCWD,PS_ALBUM_REFRESH_ROOT"/refresh.request",AT_FDCWD,claim,1)){
    struct stat claimed;
    if(lstat(claim,&claimed)||claimed.st_dev!=st.st_dev||claimed.st_ino!=st.st_ino||claimed.st_size!=st.st_size){
     fputs("PS_ALBUM_REQUEST_CHANGED_REJECTED\n",stderr);
     ((void(*)(void*))(base+PS_ALBUM_SHARED_DTOR))(&item);return;
    }
    dir[PS_ALBUM_BROWSED]=0;
    fprintf(stderr,"PS_ALBUM_RESCAN_REQUESTED token=%llu folder=%s NOT_IMPORT_ACK\n",(unsigned long long)request.token,folder);
    fd=open(result,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd>=0){dprintf(fd,"RESCAN_REQUESTED %llu\n",(unsigned long long)request.token);fsync(fd);close(fd);}
   }
  }else fputs("PS_ALBUM_RESCAN_PRECONDITION_REJECTED\n",stderr);
 }
 /* 使用原厂相同布局的共享指针析构，保留目录及异步扫描的引用生命周期。 */
 ((void(*)(void*))(base+PS_ALBUM_SHARED_DTOR))(&item);
}
