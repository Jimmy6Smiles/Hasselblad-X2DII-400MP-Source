/* 自写完整回读校验：八条独立 FNV-1a 链，解开单链的逐字节计算依赖。
 * 仅检测写入/回读不一致，不是密码学认证；SHA256 输入身份检查保持不变。
 * 总字节数决定 lane，调用分块不影响结果；不写入任何照片格式字段。 */
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
 while(n>=8){
  d->lane[0]=(d->lane[0]^p[0])*prime;d->lane[1]=(d->lane[1]^p[1])*prime;
  d->lane[2]=(d->lane[2]^p[2])*prime;d->lane[3]=(d->lane[3]^p[3])*prime;
  d->lane[4]=(d->lane[4]^p[4])*prime;d->lane[5]=(d->lane[5]^p[5])*prime;
  d->lane[6]=(d->lane[6]^p[6])*prime;d->lane[7]=(d->lane[7]^p[7])*prime;
  p+=8;n-=8;d->bytes+=8;
 }
 while(n--){unsigned k=(unsigned)(d->bytes&7);d->lane[k]=(d->lane[k]^*p++)*prime;d->bytes++;}
#endif
}
static int ps_digest_equal(const PsReadbackDigest *a,const PsReadbackDigest *b){
 return a->bytes==b->bytes&&!memcmp(a->lane,b->lane,sizeof a->lane);
}
#endif
