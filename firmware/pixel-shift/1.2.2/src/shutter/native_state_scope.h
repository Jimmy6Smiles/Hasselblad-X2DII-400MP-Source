/* Firmware-specific RAM-only scope for the bounded native-render experiment.
 * The state machine retains this event's metadata before starting its async
 * native session. Do not substitute TLS or a next-request counter for this
 * lifetime. Not a production authorization/lease implementation. */
#ifndef NATIVE_STATE_SCOPE_H
#define NATIVE_STATE_SCOPE_H
#include <link.h>
#include <sys/mman.h>
typedef void (*NssTransition)(void*,void*);
static NssTransition nss_entry_original,nss_exit_original;
static pthread_mutex_t nss_mutex=PTHREAD_MUTEX_INITIALIZER;
static void*nss_owner;
static NativeRequestToken nss_token;
static int nss_valid;
static void nss_enter(void*state,void*event){
 NativeRequestToken token={0};unsigned kind=0;void*object=NULL,*container=NULL;
 if(event){
  memcpy(&kind,(unsigned char*)event+0x68,4);
  if(kind==14){
   memcpy(&object,(unsigned char*)event+0x90,8);
   if(object)memcpy(&container,object,8);
  }
 }
 int valid=container&&read_request_token(container,&token);
 pthread_mutex_lock(&nss_mutex);
 nss_owner=state;nss_token=token;nss_valid=valid;
 pthread_mutex_unlock(&nss_mutex);
 char line[128];int n=snprintf(line,sizeof line,"NATIVE_STATE_ENTER kind=%u valid=%d serial=%u\n",kind,valid,token.serial);
 if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 nss_entry_original(state,event);
}
static void nss_exit(void*state,void*event){
 pthread_mutex_lock(&nss_mutex);
 if(nss_owner==state){nss_owner=NULL;nss_valid=0;memset(&nss_token,0,sizeof nss_token);}
 pthread_mutex_unlock(&nss_mutex);
 nss_exit_original(state,event);
}
static int nss_snapshot(NativeRequestToken*token){
 pthread_mutex_lock(&nss_mutex);int valid=nss_valid&&nss_owner;
 if(valid)*token=nss_token;
 pthread_mutex_unlock(&nss_mutex);return valid;
}
static int nss_matches(const NativeRequestToken*token){
 NativeRequestToken active={0};
 return token&&nss_snapshot(&active)&&active.nonce==token->nonce&&active.serial==token->serial&&
  active.x==token->x&&active.y==token->y;
}
/* Only two pointer slots in the main executable's RELRO, no instructions.
 * Verify exact relocated original functions and RELRO extent before changing
 * page protection. Restore read-only before constructor returns. */
static int nss_install_callback(struct dl_phdr_info*info,size_t size,void*result){
 (void)size;
 if(info->dlpi_name&&info->dlpi_name[0]&&strcmp(info->dlpi_name,"/system/bin/camera-service"))return 0;
 uintptr_t base=info->dlpi_addr,slot=base+0x8c1f70;
 long pagesize=sysconf(_SC_PAGESIZE);if(pagesize!=4096)return 0;
 uintptr_t page=slot&~(uintptr_t)(pagesize-1);int relro=0;
 for(unsigned i=0;i<info->dlpi_phnum;i++){
  const ElfW(Phdr)*p=&info->dlpi_phdr[i];
  if(p->p_type==PT_GNU_RELRO&&page>=base+p->p_vaddr&&page+4096<=base+p->p_vaddr+p->p_memsz)relro=1;
 }
 if(!relro)return 0;
 uintptr_t before[2];memcpy(before,(void*)slot,sizeof before);
 if(before[0]!=base+0x13c478||before[1]!=base+0x139cb0)return 0;
 nss_entry_original=(NssTransition)before[0];nss_exit_original=(NssTransition)before[1];
 if(mprotect((void*)page,4096,PROT_READ|PROT_WRITE))return 0;
 NssTransition replacement[2]={nss_enter,nss_exit};memcpy((void*)slot,replacement,sizeof replacement);
 if(mprotect((void*)page,4096,PROT_READ))abort();
 *(int*)result=1;return 1;
}
static int nss_install(void){int result=0;dl_iterate_phdr(nss_install_callback,&result);return result;}
#endif
