/* 自写数据表候选：不写代码页，不建立 trampoline，不请求可执行内存。
 * 仅快拍判断内唯一 stop_down getter 调用点可增加禁止条件；真实 getter、
 * 实际光圈及其他调用者不变。等待原厂自行重算和 ready/ACK，不主动曝光。 */
#include "fast_capture_guard.h"
typedef unsigned char (*FastGetterFn)(const void *);
typedef ssize_t (*WriteFn)(int,const void *,size_t);
static PsFastGuard fast_guard;
static FastGetterFn original_fast_getter;
static WriteFn original_write;
static int fast_patched,fast_write_patched;
static unsigned long fast_checks;
#ifdef PS_FAST_DIAGNOSTICS
/* 只记固定原厂调用点的计数，不存 fd 内容或任意进程数据。 */
static unsigned long fast_target_writes,fast_ready_writes,fast_target_reads,fast_ack_reads;
static unsigned fast_original_value=255;
#endif
#ifdef PS_FAST_REFRESH
#include "fast_refresh.inc"
#endif
static unsigned char fast_getter(const void *self){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0)-base;
 unsigned char value=original_fast_getter(self);
 if(caller!=PS_FAST_GETTER_RETURN)return value;
#ifdef PS_FAST_REFRESH
 fast_refresh_attach(self);
#endif
 pthread_mutex_lock(&mutex);
#ifdef PS_FAST_DIAGNOSTICS
 fast_original_value=value;
#endif
 unsigned char result=ps_fast_inhibit(&fast_guard,value,1,gate.mode,gate.session,gate.sequence);
 fast_checks++;
 pthread_mutex_unlock(&mutex);return result;
}
static ssize_t fast_write(int fd,const void *data,size_t size){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0)-base;int tracked=0;
 if(caller==PS_CAPTURE_WRITE_RETURN){
  pthread_mutex_lock(&mutex);tracked=ps_fast_sent(&fast_guard,data,size);
#ifdef PS_FAST_DIAGNOSTICS
  fast_target_writes++;if(tracked)fast_ready_writes++;
#endif
  pthread_mutex_unlock(&mutex);
 }
 ssize_t n=original_write(fd,data,size);
 if(tracked){pthread_mutex_lock(&mutex);ps_fast_write_result(&fast_guard,n);pthread_mutex_unlock(&mutex);}
 return n;
}
static void fast_read_observe(uintptr_t caller,const void *data,size_t size,size_t cap,ssize_t n){
 if((caller!=0x23a8e4&&caller!=0x23a9e8)||size!=12||cap!=12||n<4||n>12||!data)return;
 uint32_t header;memcpy(&header,data,4);
#ifdef PS_FAST_DIAGNOSTICS
 pthread_mutex_lock(&mutex);fast_target_reads++;if(header==0x401)fast_ack_reads++;pthread_mutex_unlock(&mutex);
#endif
 if(header==0x401){pthread_mutex_lock(&mutex);ps_fast_ack(&fast_guard);pthread_mutex_unlock(&mutex);}
}
static int fast_slot_restore(uintptr_t offset,uintptr_t original,int *changed){
 if(!*changed)return 1;
 uintptr_t *slot=(uintptr_t*)(base+offset),page=(uintptr_t)slot&~(uintptr_t)4095;
 if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE))return 0;
 __atomic_store_n(slot,original,__ATOMIC_RELEASE);
 if(mprotect((void*)page,4096,PROT_READ))return 0;
 *changed=0;return 1;
}
static int fast_restore(void){
#ifdef PS_FAST_REFRESH
 int c=fast_slot_restore(PS_FAST_EVENT_SLOT,(uintptr_t)original_state_event,&fast_event_patched);
#else
 int c=1;
#endif
 int a=fast_slot_restore(PS_FAST_GETTER_SLOT,(uintptr_t)original_fast_getter,&fast_patched);
 int b=fast_slot_restore(PS_CAPTURE_WRITE_SLOT,(uintptr_t)original_write,&fast_write_patched);
 return a&&b&&c;
}
static int fast_slot_install(uintptr_t offset,uintptr_t expected,uintptr_t replacement,int *changed){
 uintptr_t *slot=(uintptr_t*)(base+offset),page=(uintptr_t)slot&~(uintptr_t)4095;
 if(*slot!=expected||mprotect((void*)page,4096,PROT_READ|PROT_WRITE))return 0;
 __atomic_store_n(slot,replacement,__ATOMIC_RELEASE);*changed=1;
 return !mprotect((void*)page,4096,PROT_READ);
}
static int fast_install(void){
 ps_fast_init(&fast_guard);
 original_write=(WriteFn)dlsym(RTLD_DEFAULT,"write");
 original_fast_getter=(FastGetterFn)(base+PS_FAST_GETTER_ORIGINAL);
 if(!original_write||*(uintptr_t*)(base+PS_CAPTURE_WRITE_SLOT)!=(uintptr_t)original_write||
    *(uintptr_t*)(base+PS_FAST_GETTER_SLOT)!=(uintptr_t)original_fast_getter)return 0;
#ifdef PS_FAST_REFRESH
 original_state_event=(StateEventFn)(base+PS_FAST_EVENT_ORIGINAL);
 if(*(uintptr_t*)(base+PS_FAST_EVENT_SLOT)!=(uintptr_t)original_state_event)return 0;
 if(!fast_slot_install(PS_FAST_EVENT_SLOT,(uintptr_t)original_state_event,(uintptr_t)fast_state_event,&fast_event_patched)){
  if(!fast_restore())_exit(125);return 0;
 }
#endif
 if(!fast_slot_install(PS_CAPTURE_WRITE_SLOT,(uintptr_t)original_write,(uintptr_t)fast_write,&fast_write_patched)||
    !fast_slot_install(PS_FAST_GETTER_SLOT,(uintptr_t)original_fast_getter,(uintptr_t)fast_getter,&fast_patched)){
  if(!fast_restore())_exit(125);return 0;
 }
 return 1;
}
