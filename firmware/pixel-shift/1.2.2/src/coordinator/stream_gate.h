/* Pure coordinator admission contract. NOT a device event transport or READY
 * generator. Caller supplies authoritative reserved paths and identity; until
 * final-write semantics are validated, final_write_verified must remain zero. */
#ifndef PS_STREAM_GATE_H
#define PS_STREAM_GATE_H
#include <stdint.h>
#include <string.h>
typedef struct {uint64_t dev,ino,size,mtime_ns,ctime_ns;} SgIdentity;
typedef struct {
 uint64_t job,generation,start_ns;char paths[6][64];
 unsigned count,mask;int cancelled,fault;
 SgIdentity ids[6];uint64_t times[6];unsigned queue[6];
} StreamGate;
typedef struct {
 uint64_t job,generation,ns;const char *path;
 unsigned source,kind,error;int final_write_verified;
 SgIdentity before,after;
} SgEvent;
static inline int sg_same(SgIdentity a,SgIdentity b){
 return a.dev==b.dev&&a.ino==b.ino&&a.size==b.size&&a.mtime_ns==b.mtime_ns&&a.ctime_ns==b.ctime_ns;
}
/* 1 queued, 0 ignored/untrusted, -1 fail closed. No filesystem side effects. */
static inline int sg_offer(StreamGate *g,const SgEvent *e){
 if(g->cancelled||g->fault)return -1;
 if(!e->path||e->job!=g->job||e->generation!=g->generation||e->ns<g->start_ns)return 0;
 if(e->source!=2)return 0;
 unsigned index=6;
 for(unsigned i=0;i<6;i++)if(g->paths[i][0]&&!strcmp(e->path,g->paths[i])){index=i;break;}
 if(index==6)return 0;
 /* A rewrite may move the payload, even when its result reports failure.
  * Invalidate the speculative branch; caller may use the original serial path
  * only after native capture/drain, never reinterpret this as user cancel. */
 if(e->kind==8||(e->kind==7&&e->error!=255)){g->fault=1;return -1;}
 if(e->kind!=7||!e->final_write_verified)return 0;
 if(!e->before.ino||!e->before.size||!sg_same(e->before,e->after)){g->fault=1;return -1;}
 if(g->mask&(1u<<index)){
  if(!sg_same(g->ids[index],e->before)){g->fault=1;return -1;}
  return 0;
 }
 for(unsigned i=0;i<6;i++)if((g->mask&(1u<<i))&&g->ids[i].dev==e->before.dev&&g->ids[i].ino==e->before.ino){g->fault=1;return -1;}
 if(g->count>=6){g->fault=1;return -1;}
 g->ids[index]=e->before;g->times[index]=e->ns;
 g->queue[g->count++]=index;g->mask|=1u<<index;return 1;
}
static inline void sg_cancel(StreamGate *g){g->cancelled=1;g->count=0;}
/* Mandatory again after native six-frame completion and processing drain.
 * Caller still must validate RAW structure/data and metadata; stat equality
 * alone is not a final integrity guarantee or permission to delete originals. */
static inline int sg_revalidate(StreamGate *g,uint64_t job,uint64_t generation,
                               const SgIdentity current[6],int native_drained){
 if(g->cancelled||g->fault)return -1;
 if(!native_drained||g->mask!=63)return 0;
 if(job!=g->job||generation!=g->generation){g->fault=1;return -1;}
 for(unsigned i=0;i<6;i++)if(!sg_same(g->ids[i],current[i])){g->fault=1;return -1;}
 return 1;
}
#endif
