#ifndef NATIVE_JOB_METADATA_H
#define NATIVE_JOB_METADATA_H
#include "native_raw_domain.h"
#include "native_dcam_fixture.h"
/* Keep the fresh first frame's entire native parameter blob unchanged.
 * Derive merge sample range from it, rather than overwriting its color fields
 * with the old experiment's white balance / black / saturation constants. */
static inline int njm_range(const unsigned char*p,size_t n,NativeRawRange*out){
 if(!out)return 0;
 size_t black=nd_field(p,n,0x1772,4,1),white=nd_field(p,n,0x4c7,5,1);
 size_t b0=nd_field(p,n,0x4c2,5,4),b1=nd_field(p,n,0x4c3,5,4);
 if(!black||!white||!b0||!b1)return 0;
 NativeRawRange r={{0},nd_u32(p+white)};uint32_t value=nd_u32(p+black);
 for(unsigned c=0;c<4;c++){
  if(nd_u32(p+b0+4*c)!=value||nd_u32(p+b1+4*c)!=value)return 0;
  r.black[c]=value;
 }
 if(!nrd_valid(&r))return 0;*out=r;return 1;
}
#endif
