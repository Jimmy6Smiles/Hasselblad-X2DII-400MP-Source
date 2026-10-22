/* 私有固定固件候选：只在原厂自己的线程调用时收窄快速拍摄许可。
 * 不从发布线程调用 QObject，不注入曝光、不改 MCU 报文或 ACK。
 * 必须在 main 前安装；RX trampoline 和 RAM 指令修补均可能被策略拒绝。
 * 只等待原厂重算，不假定切 gate 就已经撤销硬件许可。 */
#include "fast_capture_guard.h"
typedef int (*FastCheckFn)(void *);
typedef ssize_t (*WriteFn)(int,const void *,size_t);
static PsFastGuard fast_guard;
static FastCheckFn original_fast_check;
static WriteFn original_write;
static void *fast_trampoline;
static unsigned char fast_saved[16];
static int fast_patched,fast_write_patched;
static unsigned long fast_checks;
static int fast_check(void *self){
 int allowed=original_fast_check(self);
 pthread_mutex_lock(&mutex);
 int result=ps_fast_check(&fast_guard,allowed,gate.mode,gate.session,gate.sequence);
 fast_checks++;
 pthread_mutex_unlock(&mutex);return result;
}
static ssize_t fast_write(int fd,const void *data,size_t size){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0)-base;int tracked=0;
 if(caller==PS_CAPTURE_WRITE_RETURN){
  pthread_mutex_lock(&mutex);tracked=ps_fast_sent(&fast_guard,data,size);pthread_mutex_unlock(&mutex);
 }
 ssize_t n=original_write(fd,data,size);
 if(tracked){pthread_mutex_lock(&mutex);ps_fast_write_result(&fast_guard,n);pthread_mutex_unlock(&mutex);}
 return n;
}
static void fast_read_observe(uintptr_t caller,const void *data,size_t size,size_t cap,ssize_t n){
 if((caller!=0x23a8e4&&caller!=0x23a9e8)||size!=12||cap!=12||n<4||n>12||!data)return;
 uint32_t header;memcpy(&header,data,4);
 if(header==0x401){pthread_mutex_lock(&mutex);ps_fast_ack(&fast_guard);pthread_mutex_unlock(&mutex);}
}
static void fast_jump(unsigned char *out,uintptr_t target){
 /* ldr x16, literal(+8); br x16; absolute address. x16 is AAPCS scratch. */
 uint32_t ins[]={0x58000050,0xd61f0200};memcpy(out,ins,8);memcpy(out+8,&target,8);
}
static int fast_restore(void){
 uintptr_t address=base+PS_FAST_CHECK;int ok=1;
 if(fast_patched){
  if(mprotect((void*)(address&~(uintptr_t)4095),4096,PROT_READ|PROT_WRITE))return 0;
  memcpy((void*)address,fast_saved,16);__builtin___clear_cache((char*)address,(char*)address+16);
  if(mprotect((void*)(address&~(uintptr_t)4095),4096,PROT_READ|PROT_EXEC))ok=0;
  fast_patched=0;
 }
 if(fast_write_patched){
  uintptr_t *slot=(uintptr_t*)(base+PS_CAPTURE_WRITE_SLOT),page=(uintptr_t)slot&~(uintptr_t)4095;
  if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE))return 0;
  __atomic_store_n(slot,(uintptr_t)original_write,__ATOMIC_RELEASE);
  if(mprotect((void*)page,4096,PROT_READ))ok=0;
  fast_write_patched=0;
 }
 if(fast_trampoline){munmap(fast_trampoline,4096);fast_trampoline=NULL;}
 return ok;
}
static int fast_install(void){
 ps_fast_init(&fast_guard);
 uintptr_t address=base+PS_FAST_CHECK,*slot=(uintptr_t*)(base+PS_CAPTURE_WRITE_SLOT);
 original_write=(WriteFn)dlsym(RTLD_DEFAULT,"write");
 if(!original_write||*slot!=(uintptr_t)original_write)return 0;
 memcpy(fast_saved,(void*)address,16);
 /* 构建器严格校验这四条原始指令都是无 PC-relative 的函数序言。 */
 fast_trampoline=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(fast_trampoline==MAP_FAILED){fast_trampoline=NULL;return 0;}
 memcpy(fast_trampoline,fast_saved,16);fast_jump((unsigned char*)fast_trampoline+16,address+16);
 __builtin___clear_cache(fast_trampoline,(char*)fast_trampoline+32);
 if(mprotect(fast_trampoline,4096,PROT_READ|PROT_EXEC)){fast_restore();return 0;}
 original_fast_check=(FastCheckFn)fast_trampoline;
 uintptr_t page=(uintptr_t)slot&~(uintptr_t)4095;
 if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE)){fast_restore();return 0;}
 __atomic_store_n(slot,(uintptr_t)fast_write,__ATOMIC_RELEASE);fast_write_patched=1;
 if(mprotect((void*)page,4096,PROT_READ)){if(!fast_restore())_exit(125);return 0;}
 if(mprotect((void*)(address&~(uintptr_t)4095),4096,PROT_READ|PROT_WRITE)){if(!fast_restore())_exit(125);return 0;}
 fast_jump((unsigned char*)address,(uintptr_t)fast_check);fast_patched=1;
 __builtin___clear_cache((char*)address,(char*)address+16);
 if(mprotect((void*)(address&~(uintptr_t)4095),4096,PROT_READ|PROT_EXEC)){if(!fast_restore())_exit(125);return 0;}
 return 1;
}
