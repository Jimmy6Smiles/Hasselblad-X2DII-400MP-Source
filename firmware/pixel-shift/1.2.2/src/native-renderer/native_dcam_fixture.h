/* X2D II 1.3.16.2 flat scalar DCAM fixture. No bus calls, no raw pointers used.
 * serialize_inner 0x119684/0x1196c0 uses a 20-byte header, NOT X2D's 16.
 * Only the scalar types observed in this firmware's native request are accepted.
 * This is a parameter adapter, not a spatial/lens calibration solution. */
#ifndef NATIVE_DCAM_FIXTURE_H
#define NATIVE_DCAM_FIXTURE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static uint32_t nd_u32(const unsigned char*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void nd_put(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static unsigned nd_unit(unsigned type){switch(type){case 1:return 1;case 3:return 2;case 4:case 5:case 6:return 4;default:return 0;}}
static int nd_validate(const unsigned char*p,size_t n){
 if(!p||n<20||n>32768)return 0;
 unsigned cap=nd_u32(p),used=nd_u32(p+4),tc=nd_u32(p+8),tags=nd_u32(p+12);
 if(!tags||tags>256||tc<tags||tc>4096||used>cap||cap>1048576||nd_u32(p+16)||20ull+28ull*tags+used!=n)return 0;
 const unsigned char*r=p+20+4*tags+used;
 for(unsigned i=0;i<tags;i++){
  unsigned off=nd_u32(p+20+4*i),type=nd_u32(r+24*i+8),count=nd_u32(r+24*i+12),unit=nd_unit(type);
  uint64_t size=(uint64_t)count*unit;
  if(!unit||!count||off>used||size>used-off)return 0;
  for(unsigned j=0;j<i;j++){
   unsigned other=nd_u32(p+20+4*j);uint64_t sz=(uint64_t)nd_unit(nd_u32(r+24*j+8))*nd_u32(r+24*j+12);
   if(nd_u32(r+24*i)==nd_u32(r+24*j)||(off<other+sz&&other<off+size))return 0;
  }
 }
 return 1;
}
static size_t nd_field(const unsigned char*p,size_t n,unsigned id,unsigned type,unsigned count){
 if(!nd_validate(p,n))return 0;
 unsigned tags=nd_u32(p+12),used=nd_u32(p+4);const unsigned char*r=p+20+4*tags+used;
 for(unsigned i=0;i<tags;i++)if(nd_u32(r+24*i)==id){
  if(nd_u32(r+24*i+8)!=type||nd_u32(r+24*i+12)!=count)return 0;
  return 20+4*tags+nd_u32(p+20+4*i);
 }
 return 0;
}
typedef struct {uint32_t wb[6],neutral[4],black,white;} NativeDcamColor;
/* All expected fields are resolved before writing, so failure leaves dst intact.
 * The caller must establish the provenance of ALL other metadata separately. */
static inline int nd_color_patch(unsigned char*dst,size_t n,const NativeDcamColor*c){
 if(!c||c->black>=c->white||c->white>65535)return 0;
 for(unsigned i=0;i<6;i++)if(!c->wb[i]||c->wb[i]>16777216)return 0;
 for(unsigned i=0;i<4;i++)if(!c->neutral[i]||c->neutral[i]>16777216)return 0;
 const unsigned ids[]={0x534,0x530,0x1772,0x4c2,0x4c3,0x4c7};
 const unsigned types[]={1,1,4,5,5,5},counts[]={24,16,1,4,4,1};size_t offsets[6];
 for(unsigned i=0;i<6;i++)if(!(offsets[i]=nd_field(dst,n,ids[i],types[i],counts[i])))return 0;
 for(unsigned i=0;i<6;i++)nd_put(dst+offsets[0]+4*i,c->wb[i]);
 for(unsigned i=0;i<4;i++)nd_put(dst+offsets[1]+4*i,c->neutral[i]);
 nd_put(dst+offsets[2],c->black);
 for(unsigned i=0;i<4;i++){nd_put(dst+offsets[3]+4*i,c->black);nd_put(dst+offsets[4]+4*i,c->black);}
 nd_put(dst+offsets[5],c->white);return 1;
}
#endif
