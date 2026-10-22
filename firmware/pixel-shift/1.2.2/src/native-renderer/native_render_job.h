/* One job's asynchronous ownership contract. Pure logic: no device calls.
 * The caller supplies a fresh nonce and monotonic time, and must authenticate
 * the request source before begin. A timeout does NOT establish DMA completion. */
#ifndef NATIVE_RENDER_JOB_H
#define NATIVE_RENDER_JOB_H
#include "native_request_token.h"
#include "native_tile_plan.h"
#define NRJ_REFERENCE_SERIAL UINT32_C(0x80000003)
typedef struct {
 uint64_t nonce,deadline;unsigned completed,inflight,cancelled,failed,reference_required,reference_complete;
} NativeRenderJob;
static inline int nrj_init(NativeRenderJob*j,uint64_t nonce,uint64_t now,uint64_t duration){
 if(!j||!nonce||!duration||duration>300000||now>UINT64_MAX-duration)return 0;
 *j=(NativeRenderJob){.nonce=nonce,.deadline=now+duration};return 1;
}
static inline int nrj_require_reference(NativeRenderJob*j){
 if(!j||!j->nonce||j->inflight||j->completed||j->cancelled||j->failed||j->reference_complete)return 0;
 j->reference_required=1;return 1;
}
static inline int nrj_begin(NativeRenderJob*j,const NativeRequestToken*t,uint64_t now){
 NativeTile tile;
 if(!j||!t||!j->nonce||j->inflight||j->cancelled||j->failed||now>=j->deadline||
    t->nonce!=j->nonce||!t->serial)return 0;
 if(t->serial==NRJ_REFERENCE_SERIAL){
  if(!j->reference_required||j->reference_complete||j->completed||t->x||t->y)return 0;
 }else if((j->reference_required&&!j->reference_complete)||!ntp_get(t->serial-1,&tile)||
    t->x!=tile.x||t->y!=tile.y||t->serial!=j->completed+1)return 0;
 j->inflight=t->serial;return 1;
}
static inline void nrj_cancel(NativeRenderJob*j){if(j)j->cancelled=1;}
/* Accept only a confirmed native completion of this precise submission.
 * On disconnected/unknown outcome caller must keep ownership until the native
 * service has been stopped and recovery independently confirmed. */
static inline int nrj_reply(NativeRenderJob*j,uint64_t nonce,unsigned serial,int success){
 if(!j||!j->nonce||nonce!=j->nonce||!serial||serial!=j->inflight)return 0;
 j->inflight=0;if(!success)j->failed=1;else if(serial==NRJ_REFERENCE_SERIAL)j->reference_complete=1;else j->completed++;
 return 1;
}
static inline int nrj_can_release(const NativeRenderJob*j){return j&&j->nonce&&!j->inflight;}
static inline int nrj_can_publish(const NativeRenderJob*j){
 return nrj_can_release(j)&&!j->cancelled&&!j->failed&&j->completed==NTP_COUNT&&(!j->reference_required||j->reference_complete);
}
#endif
