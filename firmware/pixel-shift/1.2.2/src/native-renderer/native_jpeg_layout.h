/* X2D II 1.3.16.2 ONLY. camera-storage toFramebuffer / fbhelper_setup_planes
 * 0x1cde80 / 0x1d1b80, toStreambuffer 0x1ce680. Not X2D 4.2.0 ABI.
 * Construction validates storage extents, not hardware dimensional support. */
#ifndef X2DII_NATIVE_JPEG_LAYOUT_H
#define X2DII_NATIVE_JPEG_LAYOUT_H
#include <stdint.h>
#include <string.h>
enum { NJ_FRAME_BYTES=0x330, NJ_STREAM_BYTES=0x1c8 };
static inline void nj32(unsigned char*p,unsigned o,uint32_t v){for(unsigned i=0;i<4;i++)p[o+i]=(unsigned char)(v>>(8*i));}
static inline uint32_t njget(const unsigned char*p,unsigned o){return p[o]|(uint32_t)p[o+1]<<8|(uint32_t)p[o+2]<<16|(uint32_t)p[o+3]<<24;}
static inline void njptr(unsigned char*p,unsigned o,uintptr_t v){for(unsigned i=0;i<8;i++)p[o+i]=(unsigned char)((uint64_t)v>>(8*i));}
static inline int nj_layout(unsigned char*f,unsigned char*s,uintptr_t in,uintptr_t out,
 uint32_t w,uint32_t h,uint32_t stride,uint32_t uv,uint32_t allocation,uint32_t capacity){
 uint64_t used=(uint64_t)stride*h;
 if(!f||!s||!in||!out||!w||!h||w>32768||h>32768||(w&1)||(h&1)||stride<w||
    (stride&255)||!stride||uv%stride||used>uv||uv>=allocation||
    (allocation-uv)%stride||used>allocation-uv||capacity<4)return 0;
 memset(f,0,NJ_FRAME_BYTES);memset(s,0,NJ_STREAM_BYTES);
 njptr(f,0x20,in);nj32(f,0x28,103);nj32(f,0x38,w);nj32(f,0x3c,h);
 for(unsigned plane=0;plane<2;plane++){
  unsigned off=0x40+plane*0x1c,bytes=plane?allocation-uv:uv;
  nj32(f,off,stride);nj32(f,off+4,plane?uv:0);
  nj32(f,off+12,bytes/stride);nj32(f,off+16,bytes);
 }
 nj32(f,0xb0,2);
 njptr(s,0x20,out);nj32(s,0x28,8);nj32(s,0x2c,103);
 nj32(s,0x30,w);nj32(s,0x34,h);nj32(s,0x38,capacity);
 nj32(s,0x48,w);nj32(s,0x4c,h);nj32(s,0x1b8,1);return 1;
}
#endif
