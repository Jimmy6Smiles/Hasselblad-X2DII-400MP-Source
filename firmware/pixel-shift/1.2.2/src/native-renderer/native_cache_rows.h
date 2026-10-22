/* Bounded read-only raster cache. Caller verifies provenance/hash before use.
 * Not a camera API, cache writer or photo publisher. */
#ifndef NATIVE_CACHE_ROWS_H
#define NATIVE_CACHE_ROWS_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
typedef struct {int fd;struct stat identity;const volatile sig_atomic_t*cancel;off_t offset;unsigned stride,black,white,container;} NativeCacheRows;
static inline void ncr_close(NativeCacheRows*c){if(c&&c->fd>=0){close(c->fd);c->fd=-1;}}
static inline int ncr_open(NativeCacheRows*c,const char*path,const volatile sig_atomic_t*cancel){
 if(!c||!path)return 0;
 *c=(NativeCacheRows){.fd=-1,.cancel=cancel,.stride=46620};
 c->fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(c->fd<0)return 0;
 if(fstat(c->fd,&c->identity)||!S_ISREG(c->identity.st_mode)||c->identity.st_size!=INT64_C(815010840)){
  ncr_close(c);return 0;
 }
 return 1;
}
static inline unsigned ncr_u16(const unsigned char*p){return p[0]|((unsigned)p[1]<<8);}
static inline uint32_t ncr_u32(const unsigned char*p){return ncr_u16(p)|((uint32_t)ncr_u16(p+2)<<16);}
static inline const unsigned char*ncr_tag(const unsigned char*h,unsigned ifd,unsigned tag){
 if(ifd>65530)return NULL;unsigned n=ncr_u16(h+ifd);
 if(n>128||6+12*n>65536-ifd)return NULL;const unsigned char*found=NULL;
 for(unsigned i=0;i<n;i++)if(ncr_u16(h+ifd+2+12*i)==tag){if(found)return NULL;found=h+ifd+2+12*i;}
 return found;
}
static inline int ncr_number(const unsigned char*h,unsigned ifd,unsigned tag,uint32_t*v){
 const unsigned char*p=ncr_tag(h,ifd,tag);if(!p||ncr_u32(p+4)!=1)return 0;
 unsigned t=ncr_u16(p+2);
 if(t==3)*v=ncr_u16(p+8);else if(t==4)*v=ncr_u32(p+8);
 else if(t==5){unsigned o=ncr_u32(p+8);if(o>65528||ncr_u32(h+o+4)!=1)return 0;*v=ncr_u32(h+o);}
 else return 0;return 1;
}
/* Read the exact published-to-be merged raster. No second merge, numeric
 * remapping, full-image allocation, or 815 MB duplicate cache file. */
static inline int ncr_open_container(NativeCacheRows*c,const char*path,const volatile sig_atomic_t*cancel){
 if(!c||!path)return 0;*c=(NativeCacheRows){.fd=-1,.cancel=cancel};
 c->fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(c->fd<0)return 0;
 unsigned char h[65536];size_t done=0;
 if(fstat(c->fd,&c->identity)||!S_ISREG(c->identity.st_mode))goto fail;
 while(done<sizeof h){ssize_t n=pread(c->fd,h+done,sizeof h-done,done);if(n<0&&errno==EINTR)continue;if(n<=0)goto fail;done+=(size_t)n;}
 if(h[0]!='I'||h[1]!='I'||ncr_u16(h+2)!=42)goto fail;
 uint32_t root=ncr_u32(h+4),sub,off,v,preview,count;
 if(!ncr_number(h,root,330,&sub)||!ncr_number(h,sub,273,&off)||off<1024||off>65536)goto fail;
 const unsigned tags[]={256,257,258,259,277,279};
 const unsigned values[]={23312,17482,16,1,1,23312u*17482u*2u};
 for(unsigned i=0;i<6;i++)if(!ncr_number(h,sub,tags[i],&v)||v!=values[i])goto fail;
 const unsigned char*cfa=ncr_tag(h,sub,33422),*crop=ncr_tag(h,sub,50719);
 if(!cfa||ncr_u16(cfa+2)!=1||ncr_u32(cfa+4)!=4||ncr_u32(cfa+8)!=0x02010100||
    !crop||ncr_u16(crop+2)!=3||ncr_u32(crop+4)!=2||ncr_u32(crop+8)!=1)goto fail;
 if(!ncr_number(h,sub,50714,&c->black)||!ncr_number(h,sub,50717,&c->white)||
    c->black>8192||c->white>65535||c->white<=c->black||
    !ncr_number(h,root,273,&preview)||!ncr_number(h,root,279,&count)||
    preview!=off+values[5]||preview%4096||count%4096||!count||
    (uint64_t)preview+count!=(uint64_t)c->identity.st_size)goto fail;
 c->offset=(off_t)off+2;c->stride=46624;c->container=1;return 1;
fail:ncr_close(c);return 0;
}
static inline int ncr_read_row(void*ctx,unsigned y,uint16_t*row,unsigned width){
 NativeCacheRows*c=ctx;struct stat now;
 if(!c||c->fd<0||!row||width!=23310||y>=17482||(c->cancel&&*c->cancel))return 0;
 if(fstat(c->fd,&now)||now.st_dev!=c->identity.st_dev||now.st_ino!=c->identity.st_ino||
    now.st_size!=c->identity.st_size||now.st_mtim.tv_sec!=c->identity.st_mtim.tv_sec||
    now.st_mtim.tv_nsec!=c->identity.st_mtim.tv_nsec)return 0;
 size_t done=0;unsigned char*p=(void*)row;
 while(done<46620){
  if(c->cancel&&*c->cancel)return 0;
  ssize_t n=pread(c->fd,p+done,46620-done,c->offset+(off_t)y*c->stride+done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;done+=(size_t)n;
 }
 return 1;
}
#endif
