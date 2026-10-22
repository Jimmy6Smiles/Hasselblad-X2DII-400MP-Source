/* Bit-exact alternative to ntw_fill: bounded row segments, two CFA-parity
 * caches and contiguous copies. No color, sharpening or geometry changes.
 * File identity must be checked before and after the whole window. */
#ifndef NATIVE_WINDOW_SEGMENTS_H
#define NATIVE_WINDOW_SEGMENTS_H
#include "native_tile_window.h"
typedef int (*NwsRead)(void*,unsigned,unsigned,unsigned,uint16_t*);
static int nws_fill(uint16_t*dst,size_t capacity,unsigned rw,unsigned rh,unsigned stride,
 unsigned sw,unsigned sh,unsigned ox,unsigned oy,unsigned cx,unsigned cy,
 uint16_t*a,uint16_t*b,size_t scratch,NwsRead read,void*ctx,
 int (*cancel)(void*),void*cancel_ctx){
 if(!dst||!a||!b||!read||!rw||!rh||stride<rw||sw<2||sh<2||(sw&1)||(sh&1)||
    ox>=sw||oy>=sh||(ox&1)||(oy&1)||cx>=rw||cy>=rh||!(cx&1)||(cy&1)||
    scratch<rw||(uint64_t)stride*rh>capacity)return 0;
 int64_t left=(int64_t)ox-cx,right=left+rw;
 unsigned first=left<0?0:(unsigned)left,last=right>sw?sw:(unsigned)right;
 if(first>=last||last-first<2)return 0;
 unsigned begin=(unsigned)((int64_t)first-left),end=begin+last-first;
 if(begin>rw||end>rw)return 0;
 uint16_t*cache[2]={a,b};unsigned cached[2]={UINT32_MAX,UINT32_MAX};
 for(unsigned y=0;y<rh;y++){
  if(cancel&&cancel(cancel_ctx))return 0;
  unsigned sy=ntw_edge((int64_t)oy+y-cy,sh),slot=sy&1;
  if(cached[slot]!=sy){if(!read(ctx,sy,first,last-first,cache[slot]))return 0;cached[slot]=sy;}
  uint16_t*out=dst+(size_t)y*stride;
  memcpy(out+begin,cache[slot],(size_t)(last-first)*2);
  for(unsigned x=0;x<begin;x++)out[x]=cache[slot][ntw_edge(left+x,sw)-first];
  for(unsigned x=end;x<rw;x++)out[x]=cache[slot][ntw_edge(left+x,sw)-first];
  memset(out+rw,0,(size_t)(stride-rw)*2);
 }
 return 1;
}
#endif
