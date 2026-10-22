/* Opt-in only: bound first-frame GDC export. Included inside the existing
 * native scope hook after nss_snapshot/owned_token definitions. Never selects
 * a new processing profile: this special serial uses the original pipeline. */
#if !defined(NATIVE_JOB_RUNTIME) || !defined(NATIVE_STATE_SCOPE)
#error Factory grid capture requires authenticated runtime job scope
#endif
#include "native_factory_grid_collect.h"
static int nfgh_guard(void*unused,uint64_t nonce,uint32_t serial){
 (void)unused;NativeRequestToken t={0};
 return enabled&&nss_snapshot(&t)&&t.nonce==nonce&&t.serial==serial&&!t.x&&!t.y&&owned_token(&t);
}
static int nfgh_save(uint64_t nonce,const unsigned char*p,size_t n){
 char temp[160],final[160];
 snprintf(final,sizeof final,NJL_ROOT"/grid-%016llx.bin",(unsigned long long)nonce);
 snprintf(temp,sizeof temp,NJL_ROOT"/grid-%016llx.partial",(unsigned long long)nonce);
 int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 size_t at=0;while(at<n){ssize_t w=write(fd,p+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0)break;at+=(size_t)w;}
 int ok=at==n&&!fsync(fd);if(close(fd))ok=0;
 if(ok)ok=nfgh_guard(NULL,nonce,NFGB_REFERENCE_SERIAL)&&!link(temp,final);
 if(unlink(temp))ok=0;return ok;
}
int gdc2_hal_process_async(void*handle,const void*parameters){
 typedef int(*Original)(void*,const void*);Original original=(Original)dlsym(RTLD_NEXT,"gdc2_hal_process_async");
 if(!original)return -1;
 NativeRequestToken t={0};int bound=nss_snapshot(&t)&&t.serial==NFGB_REFERENCE_SERIAL&&nfgh_guard(NULL,t.nonce,t.serial);
 int rc=original(handle,parameters);
 if(rc||!bound||!nfgh_guard(NULL,t.nonce,t.serial))return rc;
 NativeFactoryGridOps ops={0};
 ops.size=dlsym(RTLD_NEXT,"duss_hal_mem_get_size");ops.map=dlsym(RTLD_NEXT,"duss_hal_mem_map");
 ops.unmap=dlsym(RTLD_NEXT,"duss_hal_mem_unmap");ops.guard=nfgh_guard;
 unsigned char*buffer=malloc(NFGB_LIMIT);if(!buffer)return rc;
 size_t n=0;int collected=nfgc_collect(buffer,NFGB_LIMIT,&n,t.nonce,parameters,0xc8,&ops);
 int saved=collected==1&&nfgh_save(t.nonce,buffer,n);free(buffer);
 char line[160];snprintf(line,sizeof line,"FACTORY_GRID_REFERENCE nonce=%016llx collect=%d bytes=%zu saved=%d\n",(unsigned long long)t.nonce,collected,n,saved);record(line);
 return rc;
}
