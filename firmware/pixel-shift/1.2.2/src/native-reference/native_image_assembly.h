/* Full-image sink adapter. No filesystem, publication or source deletion.
 * A failed/cancelled assembly is terminal; caller must discard its private
 * output. Only all nine completed windows can authorize later publication. */
#ifndef NATIVE_IMAGE_ASSEMBLY_H
#define NATIVE_IMAGE_ASSEMBLY_H
#include "native_tile_plan.h"
typedef struct {uint64_t nonce;unsigned completed,failed,cancelled;} NativeAssembly;
typedef int (*NiaSink)(void*,unsigned,unsigned,unsigned,const unsigned char*,const unsigned char*);
typedef int (*NiaCancel)(void*);
static inline int nia_init(NativeAssembly*a,uint64_t nonce){
 if(!a||!nonce)return 0;*a=(NativeAssembly){nonce,0,0,0};return 1;
}
static inline int nia_complete(const NativeAssembly*a){
 return a&&a->nonce&&!a->failed&&!a->cancelled&&a->completed==((1u<<NTP_COUNT)-1);
}
static inline int nia_add(NativeAssembly*a,uint64_t nonce,const NativeTile*t,
 const unsigned char*src,size_t bytes,unsigned stride,size_t uv_offset,
 NiaSink sink,void*context,NiaCancel cancel){
 if(!a||a->failed||a->cancelled)return 0;
 if(!nonce||nonce!=a->nonce||!ntp_valid(t)||!src||!sink||stride<NTP_TILE_WIDTH||
    uv_offset<(uint64_t)stride*NTP_TILE_HEIGHT||uv_offset>bytes||
    (uint64_t)stride*NTP_TILE_HEIGHT>bytes-uv_offset){a->failed=1;return 0;}
 unsigned bit=1u<<(t->serial-1);if(a->completed&bit){a->failed=1;return 0;}
 for(unsigned y=t->top;y<t->bottom;y++){
  if(cancel&&cancel(context)){a->cancelled=1;return 0;}
  size_t offset=(size_t)(y-t->y)*stride+t->left-t->x;
  if(!sink(context,y,t->left,t->right-t->left,src+offset,src+uv_offset+offset)){a->failed=1;return 0;}
 }
 if(cancel&&cancel(context)){a->cancelled=1;return 0;}
 a->completed|=bit;return 1;
}
#endif
