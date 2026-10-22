/* Per-job GDC transfer, not a lens calibration cache. A nonce binds to a live,
 * authenticated first-frame request in the caller. CRC detects corruption;
 * it is NOT authentication. Decode never exposes an incomplete grid. */
#ifndef NATIVE_FACTORY_GRID_BUNDLE_H
#define NATIVE_FACTORY_GRID_BUNDLE_H
#include "native_factory_mesh.h"
#define NFGB_HEADER 112u
#define NFGB_LIMIT 262144u
#define NFGB_REFERENCE_SERIAL UINT32_C(0x80000003)
static inline void nfgb_put32(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static inline void nfgb_put64(unsigned char*p,uint64_t v){nfgb_put32(p,(uint32_t)v);nfgb_put32(p+4,(uint32_t)(v>>32));}
static inline uint64_t nfgb_u64(const unsigned char*p){return nfm_u32(p)|(uint64_t)nfm_u32(p+4)<<32;}
static inline uint32_t nfgb_crc(const unsigned char*p,size_t n){
 uint32_t c=UINT32_MAX;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^(UINT32_C(0xedb88320)&(0-(c&1)));}return ~c;
}
static inline int nfgb_decode(NativeFactoryMesh*out,uint64_t nonce,const unsigned char*p,size_t n){
 if(!out||!nonce||!p||n<NFGB_HEADER||n>NFGB_LIMIT||memcmp(p,"NX2DGDC1",8)||
    nfgb_u64(p+8)!=nonce||nfm_u32(p+16)!=NFGB_REFERENCE_SERIAL||nfm_u32(p+20)!=11656||
    nfm_u32(p+24)!=8742||nfm_u32(p+28)!=8||nfm_u32(p+32)!=n||nfm_u32(p+40)||nfm_u32(p+44))return 0;
 if(nfgb_crc(p+40,n-40)!=nfm_u32(p+36))return 0;
 NativeFactoryMesh candidate={0};if(!nfm_init(&candidate,nonce))return 0;
 size_t next=NFGB_HEADER;
 for(unsigned i=0;i<8;i++){
  uint32_t offset=nfm_u32(p+48+8*i),bytes=nfm_u32(p+52+8*i);
  if(offset!=next||bytes<128||bytes>n-next||!nfm_add(&candidate,nonce,i,p+next,bytes))goto fail;
  next+=bytes;
 }
 if(next!=n||!nfm_complete(&candidate,nonce))goto fail;
 /* Caller must pass an empty object; do not overwrite/leak a live mesh. */
 if(out->points||out->present||out->nonce)goto fail;
 *out=candidate;return 1;
fail:nfm_free(&candidate);return 0;
}
static inline size_t nfgb_pack(unsigned char*out,size_t capacity,uint64_t nonce,
 const unsigned char*const grids[8],const size_t bytes[8]){
 if(!out||!nonce||!grids||!bytes||capacity<NFGB_HEADER)return 0;
 size_t sizes[8],total=NFGB_HEADER;
 NativeFactoryMesh check={0};if(!nfm_init(&check,nonce))return 0;
 for(unsigned i=0;i<8;i++){
  if(!nfm_add(&check,nonce,i,grids[i],bytes[i]))goto fail;
  sizes[i]=(size_t)nfm_u32(grids[i]+1)+128;
  if(sizes[i]>NFGB_LIMIT-total||sizes[i]>capacity-total)goto fail;
  total+=sizes[i];
 }
 if(!nfm_complete(&check,nonce))goto fail;
 /* No aliasing: packing header/payload must not destroy input grids. */
 for(unsigned i=0;i<8;i++){
  uintptr_t a=(uintptr_t)out,b=(uintptr_t)grids[i];
  if(a>UINTPTR_MAX-total||b>UINTPTR_MAX-sizes[i]||!(a+total<=b||b+sizes[i]<=a))goto fail;
 }
 memset(out,0,NFGB_HEADER);memcpy(out,"NX2DGDC1",8);nfgb_put64(out+8,nonce);
 nfgb_put32(out+16,NFGB_REFERENCE_SERIAL);nfgb_put32(out+20,11656);nfgb_put32(out+24,8742);
 nfgb_put32(out+28,8);nfgb_put32(out+32,(uint32_t)total);
 size_t offset=NFGB_HEADER;
 for(unsigned i=0;i<8;i++){
  nfgb_put32(out+48+8*i,(uint32_t)offset);nfgb_put32(out+52+8*i,(uint32_t)sizes[i]);
  memcpy(out+offset,grids[i],sizes[i]);offset+=sizes[i];
 }
 nfgb_put32(out+36,nfgb_crc(out+40,total-40));nfm_free(&check);return total;
fail:nfm_free(&check);return 0;
}
#endif
