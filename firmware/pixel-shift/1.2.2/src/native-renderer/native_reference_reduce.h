/* Diagnostic only: reduce ONE original native NV16 render, without merging,
 * mesh warping or RGB/color transforms. Partial output must be discarded on
 * cancellation. Bounded scratch: two source rows, no full-frame copy. */
#ifndef NATIVE_REFERENCE_REDUCE_H
#define NATIVE_REFERENCE_REDUCE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static inline int nrr_reduce(const unsigned char*src,size_t bytes,unsigned sw,unsigned sh,
 unsigned stride,size_t uv,unsigned char*out,size_t capacity,unsigned dw,unsigned dh,
 int(*cancel)(void*),void*ctx){
 if(!src||!out||!sw||!sh||sw>12000||sh>9000||(sw&1)||stride<sw||!dw||!dh||
    (dw&1)||dw>sw||dh>sh||dw>1920||dh>1440||uv<(uint64_t)stride*sh||uv>bytes||
    (uint64_t)stride*sh>bytes-uv||capacity<(uint64_t)dw*dh*2)return 0;
 unsigned char*row=malloc((size_t)sw*2);uint64_t*sums=calloc((size_t)dw,4*sizeof *sums);
 if(!row||!sums){free(row);free(sums);return 0;}
 int ok=0;
 for(unsigned y=0;y<dh;y++){
  memset(sums,0,(size_t)dw*4*sizeof *sums);
  unsigned top=((uint64_t)y*sh+dh-1)/dh,bottom=((uint64_t)(y+1)*sh+dh-1)/dh;
  for(unsigned sy=top;sy<bottom;sy++){
   if(cancel&&cancel(ctx))goto done;
   memcpy(row,src+(size_t)sy*stride,sw);memcpy(row+sw,src+uv+(size_t)sy*stride,sw);
   for(unsigned x=0;x<dw;x++){
    unsigned left=((uint64_t)x*sw+dw-1)/dw,right=((uint64_t)(x+1)*sw+dw-1)/dw;
    uint64_t*s=sums+4*x;
    for(unsigned sx=left;sx<right;sx++){s[0]+=row[sx];s[1]+=row[sw+(sx&~1u)];s[2]+=row[sw+(sx&~1u)+1];s[3]++;}
   }
  }
  for(unsigned x=0;x<dw;x++){
   uint64_t*s=sums+4*x;out[(size_t)y*dw+x]=(s[0]+s[3]/2)/s[3];
   if(!(x&1)){uint64_t*t=s+4,n=s[3]+t[3];size_t p=(size_t)dw*dh+(size_t)y*dw+x;
    out[p]=(s[1]+t[1]+n/2)/n;out[p+1]=(s[2]+t[2]+n/2)/n;}
  }
 }
 ok=!(cancel&&cancel(ctx));
done:free(row);free(sums);return ok;
}
#endif
