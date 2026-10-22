/* 一次性无拍摄诊断。只在状态机 owner thread 调用原厂接口 false。
 * 不写原厂缓存、不构造假通知控制块、不把返回视作 ACK。 */
extern void ps_notification_create(uintptr_t,void *,const char *);
static unsigned fast_reconfirm_attempts,fast_reconfirm_returned,fast_reconfirm_rejected;
#ifdef PS_FAST_ARM_RECONFIRM
static uint64_t fast_reconfirm_session,fast_reconfirm_sequence;
#endif
static void fast_reconfirm_off(void *object){
 pthread_mutex_lock(&mutex);
#ifdef PS_FAST_ARM_RECONFIRM
 /* 每次显式启用至多一次；BUSY/OFF/FAILED 不发消息。已有真实禁止
  * ACK 沿用原观察器状态，不伪造允许消息，也不改原厂权限缓存。 */
 int eligible=gate.mode==1&&!gate.ready&&gate.session&&gate.sequence&&
   !fast_guard.fault&&!fast_guard.count&&fast_guard.acknowledged!=0&&
   (gate.session!=fast_reconfirm_session||gate.sequence!=fast_reconfirm_sequence);
 if(eligible){fast_reconfirm_session=gate.session;fast_reconfirm_sequence=gate.sequence;fast_reconfirm_attempts++;}
#else
 int eligible=gate.mode==3&&!gate.ready&&gate.session&&gate.sequence&&
   !fast_guard.fault&&!fast_guard.count&&fast_guard.acknowledged==-1&&
   !fast_reconfirm_attempts;
 if(eligible)fast_reconfirm_attempts=1;
#endif
 pthread_mutex_unlock(&mutex);
 if(!eligible)return;
 void *service=NULL;memcpy(&service,(char*)object+0xa30,sizeof service);
 if(*((unsigned char*)object+PS_FAST_CACHE_OFFSET)!=0||!service||
    *(uintptr_t*)service!=base+PS_SERVICE_VTABLE||!fast_thread_ok(service)||
    *(uintptr_t*)((char*)service+0x38)||
    *(uintptr_t*)(*(uintptr_t*)service+0x68)!=base+PS_SERVICE_READY){
  pthread_mutex_lock(&mutex);fast_reconfirm_rejected=1;fast_guard.fault=1;pthread_mutex_unlock(&mutex);return;
 }
 void *notification[2]={NULL,NULL};
 ps_notification_create(base+PS_NOTIFICATION_CREATE,notification,"ps-inhibit-once");
 if(notification[0]&&notification[1]){
  ((void(*)(void*,unsigned char,const void*))(base+PS_SERVICE_READY))(service,0,notification);
  pthread_mutex_lock(&mutex);fast_reconfirm_returned=1;pthread_mutex_unlock(&mutex);
 }else{
  pthread_mutex_lock(&mutex);fast_reconfirm_rejected=2;fast_guard.fault=1;pthread_mutex_unlock(&mutex);
 }
 ((void(*)(void*))(base+PS_NOTIFICATION_DESTROY))(notification);
}
