/* Box-reduce native NV16 without a custom color transform. Bounded storage:
 * 1920x1440 sums/counts = 44,236,800 bytes, not a full 407MP YUV allocation.
 * This is an assembly sink; native rendering and JPEG encoding remain callers. */
#ifndef NATIVE_PREVIEW_ASSEMBLY_H
#define NATIVE_PREVIEW_ASSEMBLY_H
#include "native_image_assembly.h"
#include <stdlib.h>
typedef struct {unsigned width,height;uint32_t*data;unsigned boundaries[1921];} NativePreview;
static inline int npa_init(NativePreview*p,unsigned w,unsigned h){
 if(!p||w<64||w>1920||(w&1)||h<48||h>1440)return 0;
 *p=(NativePreview){0};p->data=calloc((size_t)w*h,4*sizeof(uint32_t));if(!p->data)return 0;
 p->width=w;p->height=h;
 for(unsigned i=0;i<=w;i++)p->boundaries[i]=((uint64_t)i*NTP_WIDTH+w-1)/w;
 return 1;
}
static inline void npa_free(NativePreview*p){if(p){free(p->data);memset(p,0,sizeof *p);}}
static inline int npa_sink(void*context,unsigned y,unsigned x,unsigned n,const unsigned char*luma,const unsigned char*uv){
 NativePreview*p=context;
 if(!p||!p->data||!luma||!uv||y>=NTP_HEIGHT||x>NTP_WIDTH||n>NTP_WIDTH-x||!n||(x&1)||(n&1))return 0;
 unsigned dy=(uint64_t)y*p->height/NTP_HEIGHT,dx=(uint64_t)x*p->width/NTP_WIDTH,pos=x;
 while(pos<x+n){
  unsigned end=p->boundaries[dx+1];if(end>x+n)end=x+n;
  uint32_t*sum=p->data+4*((size_t)dy*p->width+dx);unsigned count=end-pos;
  for(;pos<end;pos++){unsigned i=pos-x;sum[0]+=luma[i];sum[1]+=uv[i&~1u];sum[2]+=uv[(i&~1u)+1];}
  sum[3]+=count;dx++;
 }
 return 1;
}
static inline int npa_finish(const NativePreview*p,const NativeAssembly*a,unsigned char*out,size_t bytes){
 if(!p||!p->data||!nia_complete(a)||!out||bytes<(size_t)p->width*p->height*2)return 0;
 /* No output mutation before complete coverage has been independently checked. */
 for(unsigned y=0;y<p->height;y++)for(unsigned x=0;x<p->width;x++){
  unsigned top=((uint64_t)y*NTP_HEIGHT+p->height-1)/p->height;
  unsigned bottom=((uint64_t)(y+1)*NTP_HEIGHT+p->height-1)/p->height;
  unsigned expected=(bottom-top)*(p->boundaries[x+1]-p->boundaries[x]);
  if(p->data[4*((size_t)y*p->width+x)+3]!=expected)return 0;
 }
 size_t count=(size_t)p->width*p->height;
 for(size_t i=0;i<count;i++){
  const uint32_t*s=p->data+4*i;out[i]=(unsigned char)((s[0]+s[3]/2)/s[3]);
  if(!(i&1)){const uint32_t*t=s+4;uint32_t n=s[3]+t[3];
   out[count+i]=(unsigned char)((s[1]+t[1]+n/2)/n);out[count+i+1]=(unsigned char)((s[2]+t[2]+n/2)/n);
  }
 }
 return 1;
}
#endif
