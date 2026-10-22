/* 固定固件 ABI：在合法 getter 调用线程创建 QObject 自有定时器。
 * 仅拦截该对象/该 timer id，其余事件完全透传；不从后台调用 QObject。
 * 不持有跨线程可解引用 QObject 指针；指针仅与当前合法回调参数比较。
 * 目标服务单状态机实例：观察到第二实例或线程不符即拒绝武装。 */
typedef unsigned char (*StateEventFn)(void *,void *);
typedef int (*StartTimerFn)(void *,int,int);
typedef void *(*ObjectThreadFn)(const void *);
typedef void *(*CurrentThreadFn)(void);
typedef void (*RefreshFn)(void *);
static StateEventFn original_state_event;
static int fast_event_patched,fast_timer_id;
static void *fast_timer_object;
static uint64_t fast_refresh_session,fast_refresh_sequence;
static unsigned fast_refresh_mode=99;
static unsigned long fast_refresh_calls,fast_timer_ticks;
#ifdef PS_FAST_DIAGNOSTICS
static unsigned fast_cached_before=255,fast_cached_after=255;
#endif
static int fast_thread_ok(void *object){
 return ((ObjectThreadFn)(base+PS_QOBJECT_THREAD))(object)==((CurrentThreadFn)(base+PS_CURRENT_THREAD))();
}
#ifdef PS_FAST_RECONFIRM
#include "fast_reconfirm.inc"
#endif
static void fast_refresh_attach(const void *camera){
 void *object;memcpy(&object,(const char*)camera+0x58,sizeof object);
 if(!object||*(uintptr_t*)object!=base+PS_FAST_STATE_VTABLE)return;
 if(!fast_thread_ok(object)){pthread_mutex_lock(&mutex);fast_guard.fault=1;pthread_mutex_unlock(&mutex);return;}
 pthread_mutex_lock(&mutex);
 if(fast_timer_object&&fast_timer_object!=object){fast_guard.fault=1;pthread_mutex_unlock(&mutex);return;}
 if(fast_timer_id){pthread_mutex_unlock(&mutex);return;}
 fast_timer_object=object;fast_timer_id=-1;
 pthread_mutex_unlock(&mutex);
 int id=((StartTimerFn)(base+PS_START_TIMER))(object,100,1);
 pthread_mutex_lock(&mutex);fast_timer_id=id>0?id:-1;if(id<=0)fast_guard.fault=1;pthread_mutex_unlock(&mutex);
}
static unsigned char fast_state_event(void *object,void *event){
 uint16_t type;memcpy(&type,(char*)event+8,2);
 int id=0;if(type==1)memcpy(&id,(char*)event+16,4);
 pthread_mutex_lock(&mutex);
 int own=type==1&&fast_timer_id>0&&object==fast_timer_object&&id==fast_timer_id;
 pthread_mutex_unlock(&mutex);
 if(!own)return original_state_event(object,event);
 if(!fast_thread_ok(object)){pthread_mutex_lock(&mutex);fast_guard.fault=1;pthread_mutex_unlock(&mutex);return 1;}
 pthread_mutex_lock(&mutex);
 fast_timer_ticks++;
 int changed=gate.mode!=fast_refresh_mode||gate.session!=fast_refresh_session||gate.sequence!=fast_refresh_sequence;
 if(changed){fast_refresh_mode=gate.mode;fast_refresh_session=gate.session;fast_refresh_sequence=gate.sequence;fast_refresh_calls++;}
 pthread_mutex_unlock(&mutex);
 if(changed){
#ifdef PS_FAST_DIAGNOSTICS
  unsigned before=*((const unsigned char*)object+PS_FAST_CACHE_OFFSET);
#endif
  ((RefreshFn)(base+PS_UPDATE_FAST))(object);
  unsigned off_cache=*((const unsigned char*)object+PS_FAST_CACHE_OFFSET);
  pthread_mutex_lock(&mutex);
  if(gate.mode==fast_refresh_mode&&gate.session==fast_refresh_session&&gate.sequence==fast_refresh_sequence)
   ps_fast_rebind_off(&fast_guard,off_cache,gate.mode,gate.session,gate.sequence);
  pthread_mutex_unlock(&mutex);
#ifdef PS_FAST_DIAGNOSTICS
  unsigned after=*((const unsigned char*)object+PS_FAST_CACHE_OFFSET);
  pthread_mutex_lock(&mutex);fast_cached_before=before;fast_cached_after=after;pthread_mutex_unlock(&mutex);
#endif
#ifdef PS_FAST_RECONFIRM
  fast_reconfirm_off(object);
#endif
 }
 return 1;
}
