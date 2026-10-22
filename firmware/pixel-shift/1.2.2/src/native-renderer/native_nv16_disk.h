/* Full-resolution sink for native_image_assembly.h. Private intermediate only.
 * Fixed three-column ownership checks reject overlap, holes and duplicate rows.
 * No album registration, source deletion or picture metadata is performed. */
#ifndef NATIVE_NV16_DISK_H
#define NATIVE_NV16_DISK_H
#include "native_image_assembly.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
typedef struct {int fd,failed,complete;unsigned char *coverage;uint64_t bytes;} NativeNv16Disk;
static int nnd_begin(NativeNv16Disk*s,int exclusive_fd){
 if(!s||exclusive_fd<0)return 0;
 struct stat st;if(fstat(exclusive_fd,&st)||!S_ISREG(st.st_mode)||st.st_size||st.st_nlink!=1)return 0;
 *s=(NativeNv16Disk){.fd=exclusive_fd};s->coverage=calloc(NTP_HEIGHT,1);
 return s->coverage!=NULL;
}
static int nnd_write(int fd,const unsigned char*p,size_t n,uint64_t off){
 while(n){ssize_t k=pwrite(fd,p,n,(off_t)off);if(k<0&&errno==EINTR)continue;
  if(k<=0)return 0;p+=k;n-=k;off+=(uint64_t)k;}return 1;
}
static int nnd_sink(void*context,unsigned y,unsigned x,unsigned n,const unsigned char*luma,const unsigned char*uv){
 NativeNv16Disk*s=context;unsigned bit=0;
 if(!s||!s->coverage||s->failed||s->complete)return 0;
 if(y>=NTP_HEIGHT||!luma||!uv)goto fail;
 for(unsigned col=0;col<3;col++){
  NativeTile t;ntp_get(col,&t);
  if(x==t.left&&n==t.right-t.left)bit=1u<<col;
 }
 if(!bit||(s->coverage[y]&bit))goto fail;
 uint64_t off=(uint64_t)y*NTP_WIDTH+x,plane=(uint64_t)NTP_WIDTH*NTP_HEIGHT;
 if(!nnd_write(s->fd,luma,n,off)||!nnd_write(s->fd,uv,n,plane+off))goto fail;
 s->coverage[y]|=bit;s->bytes+=(uint64_t)n*2;return 1;
fail:s->failed=1;return 0;
}
static int nnd_finish(NativeNv16Disk*s,const NativeAssembly*a){
 if(!s||!s->coverage||s->failed||s->complete||!nia_complete(a))return 0;
 uint64_t expected=(uint64_t)NTP_WIDTH*NTP_HEIGHT*2;
 if(s->bytes!=expected){s->failed=1;return 0;}
 for(unsigned y=0;y<NTP_HEIGHT;y++)if(s->coverage[y]!=7){s->failed=1;return 0;}
 struct stat st;
 if(fsync(s->fd)||fstat(s->fd,&st)||(uint64_t)st.st_size!=expected){s->failed=1;return 0;}
 s->complete=1;return 1;
}
static void nnd_release(NativeNv16Disk*s){if(s){free(s->coverage);s->coverage=NULL;}}
#endif
