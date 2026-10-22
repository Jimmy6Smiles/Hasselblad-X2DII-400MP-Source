/* Private, in-memory native request marker. Never written into photo EXIF.
 * Preserved by X2DII 1.3.16.2 RAW-writer -> R2Y metadata copies (device trial
 * 20261005-215712). Identity is a job nonce plus tile serial, not an address.
 * Caller must authenticate the active nonce and finish/cancel its lease. */
#ifndef NATIVE_REQUEST_TOKEN_H
#define NATIVE_REQUEST_TOKEN_H
#include "native_dcam_fixture.h"
#define NRT_TAG UINT32_C(0x7f583244)
#define NRT_MAGIC UINT32_C(0x58324432)
enum {NRT_WORDS=8,NRT_BYTES=32};
typedef struct {uint64_t nonce;uint32_t serial,x,y;} NativeRequestToken;
static inline int nrt_valid(const NativeRequestToken*t){
 return t&&t->nonce&&t->serial&&t->x<23310&&t->y<17482&&!(t->x&1)&&!(t->y&1);
}
static inline void nrt_words(const NativeRequestToken*t,uint32_t w[NRT_WORDS]){
 w[0]=NRT_MAGIC;w[1]=1;w[2]=(uint32_t)t->nonce;w[3]=(uint32_t)(t->nonce>>32);
 w[4]=t->serial;w[5]=t->x;w[6]=t->y;w[7]=0;
}
static inline int nrt_decode(const uint32_t w[NRT_WORDS],NativeRequestToken*t){
 if(!w||!t||w[0]!=NRT_MAGIC||w[1]!=1||w[7])return 0;
 NativeRequestToken r={(uint64_t)w[2]|(uint64_t)w[3]<<32,w[4],w[5],w[6]};
 if(!nrt_valid(&r))return 0;
 *t=r;return 1;
}
/* Transactional append. Existing fields remain byte-exact; rejects duplicate
 * token IDs even when their type is wrong, aliasing, and insufficient space. */
static inline size_t nrt_append(unsigned char*dst,size_t capacity,const unsigned char*src,
 size_t n,const NativeRequestToken*t){
 if(!dst||!src||!nrt_valid(t)||!nd_validate(src,n)||n+28+NRT_BYTES>capacity||n+28+NRT_BYTES>32768)return 0;
 uintptr_t a=(uintptr_t)dst,b=(uintptr_t)src;
 if(a<=b?b-a<n+28+NRT_BYTES:a-b<n)return 0;
 unsigned tags=nd_u32(src+12),used=nd_u32(src+4);if(tags>=256)return 0;
 const unsigned char*records=src+20+4*tags+used;
 for(unsigned i=0;i<tags;i++)if(nd_u32(records+24*i)==NRT_TAG)return 0;
 memcpy(dst,src,20);nd_put(dst,nd_u32(src)>used+NRT_BYTES?nd_u32(src):used+NRT_BYTES);
 nd_put(dst+4,used+NRT_BYTES);nd_put(dst+8,nd_u32(src+8)>tags?nd_u32(src+8):tags+1);nd_put(dst+12,tags+1);
 memcpy(dst+20,src+20,4*tags);nd_put(dst+20+4*tags,used);
 unsigned char*values=dst+20+4*(tags+1);memcpy(values,src+20+4*tags,used);
 uint32_t words[NRT_WORDS];nrt_words(t,words);for(unsigned i=0;i<NRT_WORDS;i++)nd_put(values+used+4*i,words[i]);
 unsigned char*out=values+used+NRT_BYTES;memcpy(out,records,24*tags);out+=24*tags;
 memset(out,0,24);nd_put(out,NRT_TAG);nd_put(out+8,4);nd_put(out+12,NRT_WORDS);
 return n+28+NRT_BYTES;
}
#endif
