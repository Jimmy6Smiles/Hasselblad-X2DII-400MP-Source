/* Small-preview NV16 remap. No RGB conversion, no WB/matrix/color gains.
 * Separate destination required; failed/cancelled destination MUST be discarded.
 * Source-chroma siting is explicit (0=cosited, .5=centered), not silently guessed.
 * This remains opt-in pending native chroma-filter/first-frame binding validation. */
#ifndef NATIVE_PREVIEW_DEWARP_H
#define NATIVE_PREVIEW_DEWARP_H
#include "native_factory_mesh.h"
typedef int(*NpdCancel)(void*);
static inline unsigned char npd_bilinear(const unsigned char*p,unsigned w,unsigned h,unsigned step,unsigned component,double x,double y){
 if(x<0)x=0;if(y<0)y=0;if(x>w-1)x=w-1;if(y>h-1)y=h-1;
 unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<w?ix+1:ix,jy=iy+1<h?iy+1:iy;
 double fx=x-ix,fy=y-iy;
 double v=(1-fx)*(1-fy)*p[((size_t)iy*w+ix)*step+component]+
  fx*(1-fy)*p[((size_t)iy*w+jx)*step+component]+(1-fx)*fy*p[((size_t)jy*w+ix)*step+component]+
  fx*fy*p[((size_t)jy*w+jx)*step+component];
 return (unsigned char)(v+.5);
}
static inline int npd_apply(const NativeFactoryMesh*m,uint64_t nonce,const unsigned char*source,size_t bytes,
 unsigned w,unsigned h,unsigned char*destination,size_t capacity,double chroma_siting,NpdCancel cancel,void*context){
 if(!nfm_complete(m,nonce)||!source||!destination||w<2||h<2||w>1920||h>1440||(w&1)||
    (chroma_siting!=0&&chroma_siting!=.5))return 0;
 size_t count=(size_t)w*h,length=count*2;if(bytes<length||capacity<length)return 0;
 uintptr_t s=(uintptr_t)source,d=(uintptr_t)destination;
 if(s>UINTPTR_MAX-length||d>UINTPTR_MAX-length||!(d+length<=s||s+length<=d))return 0;
 for(unsigned y=0;y<h;y++){
  if(cancel&&cancel(context))return 0;
  for(unsigned x=0;x<w;x++){
   NfmPoint at;if(!nfm_sample(m,x,y,w,h,&at))return 0;
   destination[(size_t)y*w+x]=npd_bilinear(source,w,h,1,0,at.x,at.y);
  }
  for(unsigned x=0;x<w;x+=2){
   NfmPoint at;if(!nfm_sample(m,x+chroma_siting,y,w,h,&at))return 0;
   double cx=(at.x-chroma_siting)/2;
   for(unsigned c=0;c<2;c++)destination[count+(size_t)y*w+x+c]=npd_bilinear(source+count,w/2,h,2,c,cx,at.y);
  }
 }
 return !(cancel&&cancel(context));
}
#endif
