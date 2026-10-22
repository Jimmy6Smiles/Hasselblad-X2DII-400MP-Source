/* Same eight interleaved FNV-1a lanes and streaming ABI as the baseline.
 * Registers hold the lane state across the loop; no per-8-byte struct stores.
 * Detects readback mismatch, not cryptographic authentication. */
#ifndef PS_READBACK_DIGEST_H
#define PS_READBACK_DIGEST_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {uint64_t lane[8],bytes;} PsReadbackDigest;
static void ps_digest_init(PsReadbackDigest *d){
 for(unsigned i=0;i<8;i++)d->lane[i]=UINT64_C(14695981039346656037);
 d->bytes=0;
}
static void ps_digest_update(PsReadbackDigest *d,const void *data,size_t n){
 const unsigned char *p=data;const uint64_t prime=UINT64_C(1099511628211);
#ifdef PS_REFERENCE_CHECKSUM
 while(n--){d->lane[0]=(d->lane[0]^*p++)*prime;d->bytes++;}
#else
 while(n&&(d->bytes&7)){unsigned k=(unsigned)(d->bytes&7);d->lane[k]=(d->lane[k]^*p++)*prime;d->bytes++;n--;}
 if(n>=8){
  uint64_t a=d->lane[0],b=d->lane[1],c=d->lane[2],e=d->lane[3];
  uint64_t f=d->lane[4],g=d->lane[5],h=d->lane[6],j=d->lane[7];
  size_t bulk=n&~(size_t)7;const unsigned char *end=p+bulk;
  do{
   a=(a^p[0])*prime;b=(b^p[1])*prime;c=(c^p[2])*prime;e=(e^p[3])*prime;
   f=(f^p[4])*prime;g=(g^p[5])*prime;h=(h^p[6])*prime;j=(j^p[7])*prime;
   p+=8;
  }while(p!=end);
  d->lane[0]=a;d->lane[1]=b;d->lane[2]=c;d->lane[3]=e;
  d->lane[4]=f;d->lane[5]=g;d->lane[6]=h;d->lane[7]=j;
  d->bytes+=bulk;n-=bulk;
 }
 while(n--){unsigned k=(unsigned)(d->bytes&7);d->lane[k]=(d->lane[k]^*p++)*prime;d->bytes++;}
#endif
}
static int ps_digest_equal(const PsReadbackDigest *a,const PsReadbackDigest *b){
 return a->bytes==b->bytes&&!memcmp(a->lane,b->lane,sizeof a->lane);
}
#endif
