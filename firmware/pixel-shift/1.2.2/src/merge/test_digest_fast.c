#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#define PsReadbackDigest OldDigest
#define ps_digest_init old_init
#define ps_digest_update old_update
#define ps_digest_equal old_equal
#include "baseline_digest.h"
#undef PsReadbackDigest
#undef ps_digest_init
#undef ps_digest_update
#undef ps_digest_equal
#undef PS_READBACK_DIGEST_H
#include "readback_digest.h"
static void match(OldDigest *a,PsReadbackDigest *b){assert(a->bytes==b->bytes&&!memcmp(a->lane,b->lane,sizeof a->lane));}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static volatile uint64_t consume;
int main(void){
 size_t size=1024*1024;unsigned char *p=malloc(size+64);assert(p);
 uint32_t rng=1;for(size_t i=0;i<size+64;i++){rng=rng*1664525u+1013904223u;p[i]=(unsigned char)(rng>>24);}
 unsigned tests=0;
 for(unsigned offset=0;offset<16;offset++)for(unsigned initial=0;initial<16;initial++)for(unsigned len=0;len<257;len++){
  OldDigest a;PsReadbackDigest b;old_init(&a);ps_digest_init(&b);
  old_update(&a,p,initial);ps_digest_update(&b,p,initial);
  old_update(&a,p+offset,len);ps_digest_update(&b,p+offset,len);match(&a,&b);tests++;
 }
 for(unsigned trial=0;trial<100;trial++){
  OldDigest a;PsReadbackDigest b;old_init(&a);ps_digest_init(&b);old_update(&a,p,size);
  size_t at=0;while(at<size){rng=rng*1664525u+1013904223u;size_t n=1+(rng%65537);if(n>size-at)n=size-at;ps_digest_update(&b,p+at,n);at+=n;}
  match(&a,&b);tests++;
 }
 size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
 for(size_t n=0;n<64;n++){OldDigest a;PsReadbackDigest b;old_init(&a);ps_digest_init(&b);old_update(&a,guard+page-n,n);ps_digest_update(&b,guard+page-n,n);match(&a,&b);tests++;}munmap(guard,page*2);
 printf("DIGEST_DIFFERENTIAL_PASS cases=%u streaming_and_guard_page=1\n",tests);
 for(unsigned pass=0;pass<4;pass++){
  OldDigest a;PsReadbackDigest b;old_init(&a);ps_digest_init(&b);double start=now();
  for(unsigned i=0;i<64;i++){if(pass&1)ps_digest_update(&b,p,size);else old_update(&a,p,size);}
  double elapsed=now()-start;consume=pass&1?b.lane[0]:a.lane[0];printf("DIGEST_BENCH kind=%s bytes=67108864 seconds=%.6f\n",pass&1?"register":"baseline",elapsed);fflush(stdout);
 }
 free(p);return 0;
}
