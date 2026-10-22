/* Read only a tile's contiguous span, not every full-width source row.
 * Edge replication preserves U/V phase. Callers pin source identity. */
#ifndef NV16_WINDOW_READ_H
#define NV16_WINDOW_READ_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int(*NwrRead)(void*,unsigned char*,size_t,uint64_t);
static inline int nwr_row(unsigned w,unsigned h,unsigned plane,int sx,int sy,
 unsigned stride,unsigned char*dst,NwrRead read,void*ctx){
 if(w<2||(w&1)||!h||w>32768||h>32768||plane>1||!stride||(stride&1)||stride>8192||
    !dst||!read||(sx%2)||sx< -8192||sx>(int)w+8192)return 0;
 if(sy<0)sy=0;if(sy>=(int)h)sy=(int)h-1;
 int first=sx<0?0:sx,last=sx+(int)stride;
 if(last>(int)w)last=(int)w;
 if(first>=last){
  unsigned char edge[2];unsigned at=sx<0?0:w-2;
  if(!read(ctx,edge,2,((uint64_t)plane*h+(unsigned)sy)*w+at))return 0;
  for(unsigned x=0;x<stride;x++)dst[x]=plane?edge[x&1]:edge[sx<0?0:1];
  return 1;
 }
 unsigned left=(unsigned)(first-sx),n=(unsigned)(last-first);
 if(left>stride||n>stride-left||n<2)return 0;
 if(!read(ctx,dst+left,n,((uint64_t)plane*h+(unsigned)sy)*w+(unsigned)first))return 0;
 for(unsigned x=0;x<left;x++)dst[x]=dst[left+(plane?(x&1):0)];
 for(unsigned x=left+n;x<stride;x++)dst[x]=dst[left+n-(plane?2:1)+(plane?(x&1):0)];
 return 1;
}
#endif
