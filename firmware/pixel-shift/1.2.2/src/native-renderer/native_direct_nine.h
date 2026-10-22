/* X2D II 1.3.16.2: nine JPEG ownership rectangles, same native request origins.
 * This only crops already-rendered NV16. It does NOT perform lens correction.
 * Current pre-GDC profile therefore permits diagnostic use only. */
#ifndef NATIVE_DIRECT_NINE_H
#define NATIVE_DIRECT_NINE_H
#include "native_tile_plan.h"
enum { NDN_STEP_X=8192, NDN_STEP_Y=6144 };
typedef struct { unsigned x,y,w,h,sx,sy; } NdnRect;
static inline int ndn_rect(unsigned index,NdnRect*r){
 NativeTile t;if(!r||!ntp_get(index,&t))return 0;
 unsigned x=(index%3)*NDN_STEP_X,y=(index/3)*NDN_STEP_Y;
 unsigned w=NTP_WIDTH-x,h=NTP_HEIGHT-y;
 if(w>NDN_STEP_X)w=NDN_STEP_X;if(h>NDN_STEP_Y)h=NDN_STEP_Y;
 if(x<t.x||y<t.y||x+w>t.x+NTP_TILE_WIDTH||y+h>t.y+NTP_TILE_HEIGHT)return 0;
 *r=(NdnRect){x,y,w,h,x-t.x,y-t.y};
 return !(x%16)&&!(y%8)&&!(w%2)&&!(h%2)&&!(r->sx%2);
}
/* Reuse the existing output ION allocation. No whole-frame RAM allocation,
 * no NV16 scratch file, no source-photo write. Copy Y and interleaved UV
 * forward, separately, keeping the encoder's original stride/UV offset. */
static inline int ndn_crop(unsigned index,unsigned char*p,size_t bytes,
 unsigned stride,size_t uv,int (*cancel)(void*),void*context){
 NdnRect r;
 if(!p||!ndn_rect(index,&r)||stride<NTP_TILE_WIDTH||
    uv<(uint64_t)stride*NTP_TILE_HEIGHT||uv>bytes||
    (uint64_t)stride*NTP_TILE_HEIGHT>bytes-uv)return 0;
 for(unsigned plane=0;plane<2;plane++)for(unsigned y=0;y<r.h;y++){
  if(cancel&&cancel(context))return 0;
  unsigned char*base=p+(plane?uv:0);
  memmove(base+(size_t)y*stride,base+(size_t)(r.sy+y)*stride+r.sx,r.w);
  memset(base+(size_t)y*stride+r.w,0,stride-r.w);
 }
 return 1;
}
#endif
