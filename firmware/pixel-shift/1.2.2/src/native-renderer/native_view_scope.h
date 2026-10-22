/* Small ownership contract; caller journals intent before changing live view.
 * UNKNOWN native completion must never be treated as safe to resume. */
#ifndef NATIVE_VIEW_SCOPE_H
#define NATIVE_VIEW_SCOPE_H
typedef struct {int original,changed,off_confirmed,inflight,failed;} NativeViewScope;
static inline int nvs_begin(NativeViewScope*s,int state){
 if(!s||(state!=0&&state!=1))return 0;
 *s=(NativeViewScope){.original=state};return 1;
}
static inline int nvs_off(NativeViewScope*s,int state){
 if(!s||state!=0||s->inflight)return 0;
 s->changed=s->original;s->off_confirmed=1;return 1;
}
static inline int nvs_submit(NativeViewScope*s,int state){
 if(!s||!s->off_confirmed||s->inflight||s->failed||state!=0)return 0;
 s->inflight=1;return 1;
}
static inline int nvs_complete(NativeViewScope*s,int native_success){
 if(!s||!s->inflight)return 0;
 s->inflight=0;if(!native_success)s->failed=1;return 1;
}
static inline int nvs_can_restore(const NativeViewScope*s){return s&&!s->inflight;}
#endif
