/* Speculative admission only. No publication, ownership or deletion authority.
 * The caller snapshots the album before capture and supplies membership in that
 * snapshot. A successful bind must use the existing authoritative new_six scan.
 * This module is single-threaded; the event pump owns it, not the Qt callback. */
#ifndef PS_STREAM_CANDIDATES_H
#define PS_STREAM_CANDIDATES_H
#include "stream_gate.h"
typedef struct {
 uint64_t job,generation,start_ns;
 char album[64],paths[6][64]; SgIdentity ids[6];
 unsigned count,sent,acked; int fault,cancelled,bound;
} StreamCandidates;
static inline int sc_fail(StreamCandidates *s){s->fault=1;s->bound=0;return -1;}
static inline int sc_init(StreamCandidates *s,uint64_t job,uint64_t generation,
                         uint64_t start,const char *album){
 memset(s,0,sizeof *s);
 if(!album||strlen(album)>=sizeof s->album)return sc_fail(s);
 strcpy(s->album,album);s->job=job;s->generation=generation;s->start_ns=start;
 return 0;
}
/* canonical is supplied by the strict path parser, existed by the BEFORE
 * snapshot, never by a current directory lookup. queue contains at most six.
 * Return 1 means provisional read may be scheduled, NOT final-write proof. */
static inline int sc_offer(StreamCandidates *s,const SgEvent *e,
                           int canonical,int existed){
 if(s->fault||s->cancelled)return -1;
 if(e->job!=s->job||e->generation!=s->generation||e->ns<s->start_ns||
    e->source!=2||!e->path||!canonical||existed)return 0;
 size_t n=strlen(s->album);
 if(strncmp(e->path,s->album,n)||e->path[n]!='/')return 0;
 if(e->kind!=7&&e->kind!=8)return 0; /* includes startup read completions */
 if(e->kind==8||e->error!=255)return sc_fail(s);
 if(!e->before.ino||!e->before.size||!sg_same(e->before,e->after))return sc_fail(s);
 for(unsigned i=0;i<s->count;i++){
  if(!strcmp(s->paths[i],e->path))return sg_same(s->ids[i],e->before)?0:sc_fail(s);
  if(s->ids[i].dev==e->before.dev&&s->ids[i].ino==e->before.ino)return sc_fail(s);
 }
 /* Arrival order can differ from filename order. Do not reorder already
  * prepared frames: exact final binding rejects it and selects serial fallback. */
 if(s->bound||s->count==6||strlen(e->path)>=64)return sc_fail(s);
 strcpy(s->paths[s->count],e->path);s->ids[s->count++]=e->before;return 1;
}
/* Only one outstanding READY, so a stalled parser cannot fill its input pipe.
 * Sender advances sent only after the complete short command was written. */
static inline int sc_next(const StreamCandidates *s){
 return s->fault||s->cancelled||s->sent!=s->acked||s->sent==s->count?-1:(int)s->sent;
}
static inline int sc_sent(StreamCandidates *s,unsigned index){
 if(sc_next(s)!=(int)index)return sc_fail(s);
 s->sent++;return 0;
}
static inline int sc_ack(StreamCandidates *s,unsigned index){
 if(s->cancelled||s->fault||s->sent!=s->acked+1||index!=s->acked)return sc_fail(s);
 s->acked++;return 0;
}
static inline void sc_cancel(StreamCandidates *s){s->cancelled=1;s->bound=0;}
/* lost includes observer overflow, transport truncation/reconnect and expiry.
 * Native capture completion alone is insufficient: original new_six, volume,
 * settings and source integrity checks must have succeeded before this call. */
static inline int sc_bind(StreamCandidates *s,uint64_t job,uint64_t generation,
 const char *const paths[6],const SgIdentity ids[6],int authoritative,int lost){
 if(s->fault||s->cancelled)return -1;
 if(lost||job!=s->job||generation!=s->generation)return sc_fail(s);
 if(!authoritative)return 0;
 if(s->count!=6)return sc_fail(s);
 for(unsigned i=0;i<6;i++)if(!paths[i]||strcmp(s->paths[i],paths[i])||
     !sg_same(s->ids[i],ids[i]))return sc_fail(s);
 s->bound=1;return 1;
}
static inline int sc_finish_allowed(const StreamCandidates *s){
 return !s->fault&&!s->cancelled&&s->bound&&s->acked==6;
}
#endif
