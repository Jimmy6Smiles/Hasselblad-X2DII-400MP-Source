/* Experimental native renderer input contract. Not enabled in production.
 * Keep the first frame's linear RAW domain, including below-black samples.
 * This converts numeric ranges ONLY, not WB, lens shading or pixel geometry. */
#ifndef NATIVE_RAW_DOMAIN_H
#define NATIVE_RAW_DOMAIN_H
#include <stdint.h>

typedef struct {uint32_t black[4],white;} NativeRawRange;
static inline int nrd_valid(const NativeRawRange *r){
 if(!r||!r->white||r->white>65535)return 0;
 for(unsigned c=0;c<4;c++)if(r->black[c]>=r->white)return 0;
 return 1;
}
static inline int nrd_sample(uint32_t value,unsigned channel,
 const NativeRawRange *source,const NativeRawRange *target,uint16_t *out){
 if(!out||value>65535||channel>3||!nrd_valid(source)||!nrd_valid(target))return 0;
 int64_t denominator=source->white-source->black[channel];
 int64_t numerator=(int64_t)target->black[channel]*denominator+
  ((int64_t)value-source->black[channel])*(target->white-target->black[channel]);
 /* Clamp only the actual uint16 representation, not the photographic white
  * point. Round the full destination value to nearest, ties to even. */
 if(numerator<=0){*out=0;return 1;}
 if(numerator>=65535*denominator){*out=65535;return 1;}
 int64_t whole=numerator/denominator,rest=numerator%denominator;
 *out=(uint16_t)(whole+(rest*2>denominator||(rest*2==denominator&&(whole&1))));
 return 1;
}
#endif
