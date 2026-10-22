/* 自写临时相册入口。默认观察构建仅透传，不改 Qt 对象字段。
 * PS_ALBUM_REFRESH 构建另接有界请求驱动的缓存失效；仍不写/删照片或调用拍摄。
 * 固定固件指纹匹配才加载；本模块应答不是登记或清理成功证据。 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#ifndef PS_ALBUM_HOST_TEST
#include <unistd.h>
#include <sys/mman.h>
#include <link.h>
#include "album_observer_fingerprint.h"
#endif
typedef struct {void *owner;const uint16_t *chars;int64_t length;} PsQStringView;
typedef void (*BrowseFn)(void *,const void *,int,void *,const void *);
static BrowseFn original;
static _Atomic unsigned observed;
#ifndef PS_ALBUM_HOST_TEST
static uintptr_t base;
#endif
/* 只接受标准相册路径，日志不包含文件名、照片或任意 UTF-16 内容。 */
static int album_path(const PsQStringView *q,char out[14]){
 if(!q||!q->chars||q->length!=13)return 0;
 for(unsigned i=0;i<13;i++){if(q->chars[i]>127)return 0;out[i]=(char)q->chars[i];}
 out[13]=0;
 if(out[0]!='/'||out[4]!='/'||(memcmp(out+1,"ssd",3)&&memcmp(out+1,"cfe",3)))return 0;
 if(out[5]<'1'||out[5]>'9'||out[6]<'0'||out[6]>'9'||out[7]<'0'||out[7]>'9')return 0;
 return !strcmp(out+8,"HASBL");
}
#ifdef PS_ALBUM_REFRESH
#include "album_refresh.inc"
#include "dcf_sync_runtime.inc"
#endif
static void observe(void *self,const void *path,int options,void *output,const void *message){
 char folder[14];unsigned seq=atomic_fetch_add(&observed,1);
 int canonical=album_path(path,folder);
 if(seq<32)fprintf(stderr,"PS_ALBUM_BROWSE_BEGIN seq=%u canonical=%d folder=%s options=%d\n",seq,canonical,canonical?folder:"-",options);
#ifdef PS_ALBUM_REFRESH
 dcf_before(self,path,options);
 refresh_before(self,path,options);
#endif
 original(self,path,options,output,message);
 if(seq<32)fprintf(stderr,"PS_ALBUM_BROWSE_RETURN seq=%u NOT_IMPORT_ACK\n",seq);
}
#ifndef PS_ALBUM_HOST_TEST
static int image(struct dl_phdr_info *i,size_t length,void *unused){
 (void)length;(void)unused;
 if(!i->dlpi_name||!i->dlpi_name[0]||!strcmp(i->dlpi_name,"/system/bin/camera-storage"))base=i->dlpi_addr;
 return 0;
}
__attribute__((constructor))static void init(void){
 char exe[128];ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1);
 if(n<=0||n>=(ssize_t)sizeof exe-1)return;exe[n]=0;
 if(strcmp(exe,"/system/bin/camera-storage")||getuid()!=0)return;
 dl_iterate_phdr(image,NULL);if(!base||sysconf(_SC_PAGESIZE)!=4096)return;
 for(unsigned i=0;i<sizeof ps_album_checks/sizeof *ps_album_checks;i++){
  if(memcmp((void*)(base+ps_album_checks[i].offset),ps_album_checks[i].bytes,ps_album_checks[i].length)){
   fputs("PS_ALBUM_FINGERPRINT_REJECTED\n",stderr);return;
  }
 }
 uintptr_t *slot=(uintptr_t*)(base+PS_ALBUM_SLOT);uintptr_t expected=base+PS_ALBUM_ORIGINAL;
#ifdef PS_ALBUM_REFRESH
 if(!refresh_initialize()){fputs("PS_ALBUM_THREAD_API_REJECTED\n",stderr);return;}
#endif
 if(*slot!=expected){fputs("PS_ALBUM_SLOT_REJECTED\n",stderr);return;}
 uintptr_t page=(uintptr_t)slot&~(uintptr_t)4095;original=(BrowseFn)expected;
 if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE))return;
 __atomic_store_n(slot,(uintptr_t)observe,__ATOMIC_RELEASE);
 if(mprotect((void*)page,4096,PROT_READ)){
  __atomic_store_n(slot,expected,__ATOMIC_RELEASE);
  if(mprotect((void*)page,4096,PROT_READ))_exit(125);
  return;
 }
#ifdef PS_ALBUM_REFRESH
 fputs("PS_ALBUM_REFRESH_INSTALLED EXPLICIT_REQUEST_ONLY\n",stderr);
 /* 只写本次 RAM 目录，不覆盖旧进程的凭据；消费者仍验证存活及 maps。 */
 int ready=open(PS_ALBUM_REFRESH_ROOT"/ready",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 if(ready>=0){int ok=dprintf(ready,"ALBUM_RUNTIME_V1 %ld\n",(long)getpid())>0&&!fsync(ready);close(ready);
  if(!ok)fputs("PS_ALBUM_READY_WRITE_FAILED\n",stderr);
 }else fputs("PS_ALBUM_READY_CREATE_FAILED\n",stderr);
#else
 fputs("PS_ALBUM_OBSERVER_INSTALLED PASSTHROUGH_ONLY\n",stderr);
#endif
}
#endif
