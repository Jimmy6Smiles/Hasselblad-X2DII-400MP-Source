/* Private experimental adapter: GRBG merged raster -> native RGGB window.
 * Numeric/color/lens calibration is the caller's responsibility. This does not
 * assert that repeating the original sensor calibration per tile is correct. */
#ifndef NATIVE_TILE_WINDOW_H
#define NATIVE_TILE_WINDOW_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int (*NtwReadRow)(void*,unsigned,uint16_t*,unsigned);
static inline unsigned ntw_edge(int64_t p,unsigned size){
 if(p<0)return (unsigned)((uint64_t)p&1u);
 if((uint64_t)p>=size)return size-2+((unsigned)p&1u);
 return (unsigned)p;
}
static int ntw_fill(uint16_t *dst,size_t dst_samples,unsigned rw,unsigned rh,unsigned stride,
 unsigned sw,unsigned sh,unsigned ox,unsigned oy,unsigned crop_x,unsigned crop_y,
 uint16_t *row,size_t row_samples,NtwReadRow read_row,void*context){
 if(!dst||!row||!read_row||!rw||!rh||stride<rw||sw<2||sh<2||(sw&1)||(sh&1)||
    ox>=sw||oy>=sh||(ox&1)||(oy&1)||crop_x>=rw||crop_y>=rh||
    !(crop_x&1)||(crop_y&1)||row_samples<sw||(uint64_t)stride*rh>dst_samples)return 0;
 /* The source CFA is GRBG; odd crop_x and even crop_y put its G at native
  * input x=odd,y=even. Edge replication keeps same CFA parity. */
 unsigned cached=UINT32_MAX;
 for(unsigned y=0;y<rh;y++){
  unsigned sy=ntw_edge((int64_t)oy+y-crop_y,sh);
  if(cached!=sy){if(!read_row(context,sy,row,sw))return 0;cached=sy;}
  uint16_t*out=dst+(size_t)y*stride;
  for(unsigned x=0;x<rw;x++)out[x]=row[ntw_edge((int64_t)ox+x-crop_x,sw)];
  memset(out+rw,0,(stride-rw)*sizeof(*out));
 }
 return 1;
}
#endif
