/* 私有体验版机内任务服务：仅 loopback，固定命令，唯一物理快门请求来源。
 * 不监听外网、不接受文件路径；登记及核验成功才清本次原片。待恢复任务阻止再次武装。
 * 必须由隔离监督器管理生命周期；此文件不修改原厂启动项。 */
#define PS_JOB_TEST
#include "integrated_job.c"
#include "job_progress.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#ifdef PS_SHARED_CAPTURE
#include "capture_broker.h"
#include "capture_lease.h"
#ifndef PS_CAPTURE_BROKER
#define PS_CAPTURE_BROKER "@cm-capture-v1"
#endif
#endif
#ifndef SHUTTER
#define SHUTTER "/dev/x2d2-shutter-v1"
#endif
typedef struct {uint64_t magic,session,sequence,deadline;uint32_t mode,ready;} WireGate;
_Static_assert(sizeof(WireGate)==40,"shutter wire ABI");
static uint64_t session,sequence=1,jobid;
static int armed,busy,settled=1,recovery,delay=2,keep,listenfd=-1;
static int boot_recovery_block;
static pid_t child=-1;
static unsigned long heartbeat;
static int64_t heartbeat_changed;
static char error_text[128]="";
static pid_t readiness_child=-1;
static int64_t readiness_started,readiness_checked;
static int readiness_ok;
static int live_hook(unsigned wanted);
static int fast_blocked(void);
static int64_t arm_started;
static int64_t delay_deadline;
static pid_t boot_review_child=-1;
static int64_t boot_review_started,boot_review_checked;
#ifdef PS_SHARED_CAPTURE
static int capture_broker=-1,capture_held;
static uint64_t capture_generation;
static int shared_capture_acquire(uint64_t generation){
 if(capture_broker<0||capture_held||!generation)return 0;
 CmBrokerRequest q={CM_BROKER_MAGIC,generation,generation,CM_BROKER_ACQUIRE,CM_CAPTURE_PS};CmBrokerReply r;
 if(!cm_capture_broker_request(capture_broker,&q,&r)||r.status!=CM_BROKER_OK)return 0;
 capture_held=1;capture_generation=generation;return 1;
}
static int shared_capture_release(void){
 if(!capture_held)return 1;CmBrokerReply r;
 CmBrokerRequest q={CM_BROKER_MAGIC,capture_generation,capture_generation,CM_BROKER_RELEASE,CM_CAPTURE_PS};
 if(!cm_capture_broker_request(capture_broker,&q,&r)||r.status!=CM_BROKER_OK)return 0;
 capture_held=0;capture_generation=0;return 1;
}
#endif
static void update_boot_recovery(void){
 if(boot_review_child>0){
  int result=0;pid_t got=waitpid(boot_review_child,&result,WNOHANG);
  if(got==boot_review_child){
   boot_review_child=-1;boot_review_checked=clock_ms();struct stat st;
   if(WIFEXITED(result)&&WEXITSTATUS(result)==0&&lstat(PS_DATA"/pending",&st)<0&&errno==ENOENT){
    boot_recovery_block=0;recovery=0;settled=1;error_text[0]=0;readiness_checked=0;
   }
  }else if(got<0&&errno!=EINTR){boot_review_child=-1;boot_review_checked=clock_ms();}
  else if(clock_ms()-boot_review_started>15000){kill(-boot_review_child,SIGKILL);kill(boot_review_child,SIGKILL);}
  return;
 }
 /* Review reads factory state and journals, never triggers capture. Waiting
  * for OFF here deadlocks when a persisted menu journal keeps the gate BUSY:
  * mode restoration itself waits for this review to finish. Keep the gate
  * blocked until the independent review proves restoration. */
 if(stopping||!boot_recovery_block||!recovery||busy||armed||clock_ms()-boot_review_checked<5000)return;
 boot_review_child=fork();boot_review_started=clock_ms();
 if(!boot_review_child){
  setpgid(0,0);close(listenfd);
  execl(PS_BIN"/reboot-reconcile",PS_BIN"/reboot-reconcile","--resume",(char*)0);_exit(127);
 }
 if(boot_review_child>0)setpgid(boot_review_child,boot_review_child);
 else boot_review_checked=clock_ms();
}
/* 启用不是拍摄事务。确认超时必须退出待武装，不能无限等待或伪造 ACK。 */
static void check_arm_timeout(void);
#include "async_prepare_service.inc"
#include "mode_prepare_service.inc"
#ifdef PS_SERVICE_HOST_ALBUM_FAKE
#ifdef __ANDROID__
#error host album fixture cannot be built for Android
#endif
static int service_album_ready(void){return access(PS_BIN"/mock-album-ready",F_OK)==0;}
#else
static int service_album_ready(void){return album_runtime_ready();}
#endif
static int readiness_fresh(void){return readiness_ok&&clock_ms()-readiness_checked<8000;}
/* 独立只读子进程，不阻塞门控心跳；失败不能进入 busy 或创建拍摄任务。 */
static void update_readiness(void){
 int64_t now=clock_ms();
 if(readiness_child>0){
  int result=0;pid_t got=waitpid(readiness_child,&result,WNOHANG);
  if(got==readiness_child){
   readiness_ok=WIFEXITED(result)&&WEXITSTATUS(result)==0;
   readiness_child=-1;readiness_checked=now;
   if(!readiness_ok&&!recovery)strcpy(error_text,"CAMERA_NOT_READY_OR_ACTIVE_ERROR");
   else if(!strcmp(error_text,"CAMERA_NOT_READY_OR_ACTIVE_ERROR"))error_text[0]=0;
  }else if(got<0&&errno!=EINTR){readiness_child=-1;readiness_ok=0;readiness_checked=now;}
  else if(now-readiness_started>4000){kill(-readiness_child,SIGKILL);kill(readiness_child,SIGKILL);}
  return;
 }
 if(stopping||armed||busy||recovery||!live_hook(0)||now-readiness_checked<3000)return;
 readiness_child=fork();readiness_started=now;
 if(readiness_child==0){
  setpgid(0,0);close(listenfd);
  int fd=open(PS_BIN"/readiness.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)_exit(120);dup2(fd,1);dup2(fd,2);close(fd);
  execl(PS_BIN"/readiness-check",PS_BIN"/readiness-check",(char*)0);_exit(127);
 }
 if(readiness_child>0)setpgid(readiness_child,readiness_child);
 else{readiness_ok=0;readiness_checked=now;}
}
static int exact_write(const char *path,const void *data,size_t size){
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);struct stat st;
 if(fd<0)return 0;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&write(fd,data,size)==(ssize_t)size;close(fd);return ok;
}
static int gate(unsigned mode,unsigned ready){
#ifdef PS_SERVICE_ARM_ONLY
 ready=0; /* 临时菜单验收：即使误按快门也不得排入采集任务。 */
#endif
 WireGate g={UINT64_C(0x315447485350),session,sequence,(uint64_t)clock_ms()+1500,mode,ready};
 return exact_write(SHUTTER"/gate.next",&g,sizeof g)&&!rename(SHUTTER"/gate.next",SHUTTER"/gate");
}
static int read_json(char *b,size_t cap){
 int fd=open(SHUTTER"/status.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 ssize_t n=read(fd,b,cap-1);close(fd);if(n<2)return 0;b[n]=0;return b[n-1]=='\n'&&b[n-2]=='}';
}
static int number(const char *b,const char *name,uint64_t *v){
 char key[64];snprintf(key,sizeof key,"\"%s\":",name);const char *p=strstr(b,key);if(!p)return 0;p+=strlen(key);
 if(*p<'0'||*p>'9')return 0;char *end;errno=0;*v=strtoull(p,&end,10);return !errno&&(*end==','||*end=='}');
}
static int live_hook(unsigned wanted){
 char b[1024];uint64_t h,m;if(!read_json(b,sizeof b)||!strstr(b,"\"installed\":true")||!number(b,"heartbeat",&h)||!number(b,"mode",&m))return 0;
 if(!strstr(b,"\"fast_guard_installed\":true")||!strstr(b,"\"fast_fault\":0")||!strstr(b,"\"failed\":0"))return 0;
 int64_t tick=clock_ms();if(h!=heartbeat){heartbeat=(unsigned long)h;heartbeat_changed=tick;}
 return m==wanted&&heartbeat_changed&&tick-heartbeat_changed<500;
}
static int fast_blocked(void){
 char b[1024];uint64_t owner,seq;return read_json(b,sizeof b)&&strstr(b,"\"fast_blocked\":true")!=NULL&&
  number(b,"fast_session",&owner)&&number(b,"fast_sequence",&seq)&&owner==session&&seq==sequence;
}
static void check_arm_timeout(void){
 if(!armed||busy||recovery||fast_blocked()||clock_ms()-arm_started<=5000)return;
 armed=0;readiness_ok=0;readiness_checked=0;
 strcpy(error_text,"FAST_CAPTURE_GUARD_UNCONFIRMED");
 if(!gate(mode_preparation_blocks_normal()?2:0,0)){recovery=1;strcpy(error_text,"GATE_WRITE_FAILED");}
}
static int preflight(void){
 char b[128];char *args[]={PS_BIN"/integrated-job","--preflight",NULL};
 return service_album_ready()&&run(args,b,sizeof b,12000,-1)&&!strncmp(b,"READY format=",13);
}
static void status(char *out,size_t cap){
 snprintf(out,cap,"{\"schema\":1,\"connected\":true,\"experimentalReady\":%s,\"armed\":%s,\"busy\":%s,\"settled\":%s,\"recovery\":%s,\"jobToken\":\"%llu\",\"generationToken\":\"%llu\",\"cancelEnabled\":%s,\"error\":\"%s\",\"retainedInputsTrial\":false}",
  !recovery&&!busy&&(armed||!mode_preparation_blocks_normal())&&service_album_ready()&&(armed||readiness_fresh())&&live_hook(armed?1:0)?"true":"false",armed&&!recovery?"true":"false",busy?"true":"false",settled?"true":"false",recovery?"true":"false",(unsigned long long)jobid,(unsigned long long)session,busy&&(child>0||shot_waiting)?"true":"false",error_text);
 size_t route_n=strlen(out);snprintf(out+route_n-1,cap-route_n+1,",\"modeTransitionPending\":%s}",(!armed&&mode_preparation_blocks_normal())?"true":"false");
 size_t n=strlen(out);
 JobProgress progress=job_progress(jobid);
 int remaining=busy&&!recovery&&!progress.capture_verified?ps_delay_remaining(delay_deadline,clock_ms()):0;
 if(n&&out[n-1]=='}')snprintf(out+n-1,cap-n+1,",\"initialDelay\":%d,\"keepMode\":%s,\"cleanupAfterSave\":true,\"phase\":\"%s\",\"captureVerified\":%s,\"delayRemaining\":%d}",delay,keep?"true":"false",recovery?"recovery":remaining?"delay":shot_waiting?"prepare":progress.phase,progress.capture_verified?"true":"false",remaining);
}
static void request_job(void){
#ifdef PS_SERVICE_ARM_ONLY
 return; /* 第二道测试边界：不消费任何快门请求，不创建拍摄事务。 */
#endif
 if(!armed||busy||recovery||!live_hook(1)||!fast_blocked())return;
 int fd=open(SHUTTER"/request",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
 WireGate q;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size==sizeof q&&read(fd,&q,sizeof q)==sizeof q;close(fd);
 if(!ok||q.magic!=UINT64_C(0x315447485350)||q.session!=session||q.sequence!=sequence||q.mode!=1||q.ready!=1){recovery=1;armed=0;strcpy(error_text,"REQUEST_IDENTITY_UNCONFIRMED");gate(4,0);return;}
 jobid=(uint64_t)clock_ms()*1000+(unsigned)getpid()%1000;
#ifdef PS_SHARED_CAPTURE
 if(!shared_capture_acquire(jobid)){armed=0;strcpy(error_text,"CAPTURE_OWNER_BUSY");gate(0,0);return;}
#endif
 delay_deadline=clock_ms()+(int64_t)delay*1000;
 char jobtoken[32],delaytoken[8],deadlinetoken[32];snprintf(jobtoken,sizeof jobtoken,"%llu",(unsigned long long)jobid);snprintf(delaytoken,sizeof delaytoken,"%d",delay);snprintf(deadlinetoken,sizeof deadlinetoken,"%lld",(long long)delay_deadline);
 int pending=open(PS_DATA"/pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(pending<0){
#ifdef PS_SHARED_CAPTURE
  shared_capture_release();
#endif
  recovery=1;armed=0;strcpy(error_text,"PENDING_ALREADY_EXISTS");gate(4,0);return;}
 ok=write(pending,jobtoken,strlen(jobtoken))==(ssize_t)strlen(jobtoken)&&!fsync(pending);close(pending);
 if(!ok||!sync_dir(PS_DATA)||!gate(2,0)){recovery=1;armed=0;strcpy(error_text,"PENDING_DURABILITY_FAILED");return;}
 busy=1;settled=0;armed=0;shot_waiting=1;wait_cancel=0;cancel_cause[0]=0;
 /* 请求先改名留证，不删除/重发；每个物理按键序号只消费一次。 */
 char claimed[256];snprintf(claimed,sizeof claimed,PS_BIN"/request-%s",jobtoken);
 if(!exclusive_rename(SHUTTER"/request",claimed)){recovery=1;busy=0;strcpy(error_text,"REQUEST_CLAIM_FAILED");return;}
 /* The countdown begins now; preparation continues without a second press. */
}
/* Bounded, best-effort diagnostics; never prevent safe cancellation. */
static void retain_cancel_log(const char *folder,const char *name){
 char src[320],dst[320],data[4096];struct stat st;
 snprintf(src,sizeof src,PS_BIN"/%s",name);snprintf(dst,sizeof dst,"%s/%s",folder,name);
 int in=open(src,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(in<0)return;
 if(fstat(in,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1){close(in);return;}
 if(st.st_size>(off_t)sizeof data)lseek(in,st.st_size-sizeof data,SEEK_SET);
 ssize_t n=read(in,data,sizeof data);close(in);if(n<=0)return;
 int out=open(dst,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(out>=0){(void)write(out,data,(size_t)n);fsync(out);close(out);}
}
static void launch_waiting_job(void){
 if(!shot_waiting)return;
 if(stopping||recovery)note_cancel(stopping?"SERVICE_STOP":"RECOVERY_REQUIRED",0);
 if(wait_cancel){
  stop_menu_check();
  if(mode_prepare_child>0||mode_record_exists()||menu_check_child>0)return;
  char folder[256],record[256];
  snprintf(folder,sizeof folder,PS_DATA"/jobs/%llu",(unsigned long long)jobid);
  snprintf(record,sizeof record,"%s/cancelled.pending",folder);
  struct stat own;
  int made=mkdir(folder,0700)==0;
  if((!made&&(errno!=EEXIST||lstat(folder,&own)||!S_ISDIR(own.st_mode)||own.st_uid!=getuid()||(own.st_mode&0077)))||!exclusive_rename(PS_DATA"/pending",record)||!sync_dir(folder)||!sync_dir(PS_DATA)){
   recovery=1;strcpy(error_text,"WAIT_CANCEL_COMMIT_FAILED");return;
  }
#ifdef PS_SHARED_CAPTURE
  if(!shared_capture_release()){recovery=1;strcpy(error_text,"CAPTURE_OWNER_RELEASE_FAILED");return;}
#endif
  shot_waiting=0;busy=0;settled=1;sequence++;readiness_ok=0;readiness_checked=0;
  char diagnostic[320];snprintf(diagnostic,sizeof diagnostic,"%s/cancel-reason.log",folder);
  int diagnostic_fd=open(diagnostic,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(diagnostic_fd>=0){dprintf(diagnostic_fd,"cause=%s\nlast_error=%s\nmenu_ok=%d mode_prepared=%d\n",cancel_cause[0]?cancel_cause:"UNCLASSIFIED",error_text,menu_check_ok,mode_prepared);fsync(diagnostic_fd);close(diagnostic_fd);}
  retain_cancel_log(folder,"menu-storage-prepare.log");retain_cancel_log(folder,"mode-prepare.log");sync_dir(folder);
  strcpy(error_text,"CANCELLED_INPUTS_RETAINED");gate(0,0);return;
 }
 if(!menu_check_ok||menu_check_child>0||!mode_preparation_ready())return;
 char jobtoken[32],delaytoken[8],deadlinetoken[32];
 snprintf(jobtoken,sizeof jobtoken,"%llu",(unsigned long long)jobid);
 snprintf(delaytoken,sizeof delaytoken,"%d",delay);
 snprintf(deadlinetoken,sizeof deadlinetoken,"%lld",(long long)delay_deadline);
 child=fork();
 if(child<0){note_cancel("JOB_FORK_FAILED",errno);strcpy(error_text,"JOB_FORK_FAILED");return;}
 if(!child){
  close(listenfd);setpgid(0,0);char *av[]={PS_BIN"/integrated-job","--run-once",jobtoken,delaytoken,deadlinetoken,NULL};execv(av[0],av);_exit(127);
 }
 setpgid(child,child);shot_waiting=0;mode_prepared=0;
}

static void reap_job(void){
 if(child<=0)return;int result=0;pid_t got=waitpid(child,&result,WNOHANG);if(got==0||(got<0&&errno==EINTR))return;
 child=-1;busy=0;
 /* Exit 20 is emitted only before camera settings or exposure were touched.
  * Menu preparation may still own Auto6: release it before completing task. */
 if(got>0&&WIFEXITED(result)&&WEXITSTATUS(result)==20){
  armed=0;busy=1;shot_waiting=1;note_cancel("PREFLIGHT_REJECTED_NO_CAPTURE",20);return;
 }
 if(got>0&&WIFEXITED(result)&&(WEXITSTATUS(result)==130||WEXITSTATUS(result)==131)){
  int failed=WEXITSTATUS(result)==131;
  char record[256],folder[256];snprintf(folder,sizeof folder,PS_DATA"/jobs/%llu",(unsigned long long)jobid);
  snprintf(record,sizeof record,PS_DATA"/jobs/%llu/%s.pending",(unsigned long long)jobid,failed?"failed-idle":"cancelled");
  if(!exclusive_rename(PS_DATA"/pending",record)||!sync_dir(folder)||!sync_dir(PS_DATA)){
   recovery=1;strcpy(error_text,"CANCEL_COMMIT_UNCONFIRMED");gate(4,0);return;
  }
#ifdef PS_SHARED_CAPTURE
  if(!shared_capture_release()){recovery=1;strcpy(error_text,"CAPTURE_OWNER_RELEASE_FAILED");gate(4,0);return;}
#endif
  settled=1;armed=0;sequence++;readiness_ok=0;readiness_checked=0;
  if(!gate(mode_preparation_blocks_normal()?2:0,0)){recovery=1;strcpy(error_text,"GATE_WRITE_FAILED");return;}
  strcpy(error_text,failed?"FAILED_IDLE_INPUTS_RETAINED":"CANCELLED_INPUTS_RETAINED");return;
 }
 if(got<0||!WIFEXITED(result)||WEXITSTATUS(result)!=0){recovery=1;strcpy(error_text,"JOB_FAILED_REVIEW_CLEANUP_JOURNAL");gate(4,0);return;}
 if(unlink(PS_DATA"/pending")||!sync_dir(PS_DATA)){recovery=1;strcpy(error_text,"JOB_COMMIT_UNCONFIRMED");gate(4,0);return;}
#ifdef PS_SHARED_CAPTURE
 if(!shared_capture_release()){recovery=1;strcpy(error_text,"CAPTURE_OWNER_RELEASE_FAILED");gate(4,0);return;}
#endif
 settled=1;sequence++;armed=keep&&!mode_record_exists();arm_started=clock_ms();gate(armed?1:mode_preparation_blocks_normal()?2:0,0);
 strcpy(error_text,"SAVED_DEFAULT_FOLDER_SIX_DELETED");
}
static void client(int fd){
 struct timeval tv={0,200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
 char b[1024],reply[1024],json[768];ssize_t n=recv(fd,b,sizeof b-1,0);if(n<=0)return;b[n]=0;
 int ok=0;char path[128]={0},verb[8]={0};
 if(sscanf(b,"%7s %127s HTTP/1.1",verb,path)!=2)return;
 if(!strcmp(verb,"GET")&&!strcmp(path,"/status"))ok=1;
 else if(!strcmp(verb,"POST")&&strcasestr(b,"\r\nX-PixelShift-Local: 1\r\n")){
  int d,k,used=0;
  if(sscanf(path,"/arm?delay=%2d&keep=%1d%n",&d,&k,&used)==2&&path[used]==0&&d>=2&&d<=60&&(k==0||k==1)&&!busy&&!recovery){
   if(!armed&&menu_check_child<0&&!mode_preparation_blocks_normal()&&readiness_fresh()&&live_hook(0)){delay=d;keep=k;sequence++;armed=1;arm_started=clock_ms();menu_check_ok=0;menu_check_finished=0;error_text[0]=0;ok=gate(1,0);if(!ok){armed=0;recovery=1;}}
  }else if(sscanf(path,"/options?delay=%2d&keep=%1d%n",&d,&k,&used)==2&&path[used]==0&&d>=2&&d<=60&&(k==0||k==1)&&armed&&!busy&&!recovery&&live_hook(1)){
   delay=d;keep=k;ok=1; /* 单线程更新；不切门控、不重放快门。 */
  }else if(!strcmp(path,"/disarm")&&!busy&&!recovery){armed=0;stop_menu_check();ok=gate(mode_preparation_blocks_normal()?2:0,0);if(ok&&!strcmp(error_text,"FAST_CAPTURE_GUARD_UNCONFIRMED"))error_text[0]=0;}
  else if(!strcmp(path,"/cancel")&&busy){if(shot_waiting){note_cancel("API_CANCEL_REQUEST",0);ok=1;}else if(child>0)ok=!kill(child,SIGTERM);}
 }
 status(json,sizeof json);int count=snprintf(reply,sizeof reply,"HTTP/1.1 %s\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s",ok?"200 OK":"409 Conflict",strlen(json),json);
 if(count>0&&(size_t)count<sizeof reply)send(fd,reply,(size_t)count,MSG_NOSIGNAL);
}
int main(int argc,char **argv){
 if(argc!=2||strcmp(argv[1],"--resident-retain-inputs"))return 2;
 signal(SIGTERM,sigstop);signal(SIGINT,sigstop);signal(SIGPIPE,SIG_IGN);
 int lock=open(PS_DATA"/service.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);struct stat st;
 if(lock<0||fstat(lock,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||flock(lock,LOCK_EX|LOCK_NB))return 2;
#ifdef PS_SHARED_CAPTURE
 capture_broker=cm_capture_broker_connect(PS_CAPTURE_BROKER);if(capture_broker<0){close(lock);return 2;}
#endif
 session=(uint64_t)clock_ms()*1000+(unsigned)getpid()%1000;
 recovery=lstat(PS_DATA"/pending",&st)==0;if(recovery){boot_recovery_block=1;settled=1;strcpy(error_text,"REBOOT_RECOVERY_UNRESOLVED_CAPTURE_DISABLED");}
 listenfd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listenfd<0)return 2;
 int reuse=1;if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse))return 2;
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(38408),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(bind(listenfd,(struct sockaddr*)&addr,sizeof addr)||listen(listenfd,4))return 2;
 while(!stopping||busy||(!recovery&&mode_preparation_blocks_normal())){
  if(stopping)armed=0;
  if(stopping&&child>0)kill(child,SIGTERM);
  unsigned mode=(recovery&&!boot_recovery_block)?4:busy?2:armed?1:mode_preparation_blocks_normal()?2:0;
  if(!gate(mode,armed&&!busy&&!recovery&&live_hook(1)&&fast_blocked())){recovery=1;armed=0;strcpy(error_text,"GATE_WRITE_FAILED");}
  /* 等原厂线程重算及硬件确认；没有确认绝不把菜单标作已武装。可显式退出。 */
  check_arm_timeout();
  live_hook(mode);update_boot_recovery();update_readiness();update_menu_check();update_mode_preparation();request_job();launch_waiting_job();reap_job();
  struct pollfd p={listenfd,POLLIN,0};if(poll(&p,1,50)>0){int fd=accept4(listenfd,NULL,NULL,SOCK_CLOEXEC);if(fd>=0){client(fd);close(fd);}}
 }
 if(menu_check_child>0){stop_menu_check();while(waitpid(menu_check_child,NULL,0)<0&&errno==EINTR){}}
 if(readiness_child>0){kill(-readiness_child,SIGKILL);kill(readiness_child,SIGKILL);while(waitpid(readiness_child,NULL,0)<0&&errno==EINTR){}}
 if(boot_review_child>0){kill(-boot_review_child,SIGKILL);kill(boot_review_child,SIGKILL);while(waitpid(boot_review_child,NULL,0)<0&&errno==EINTR){}}
 if(!recovery)gate(mode_preparation_blocks_normal()?2:0,0);close(listenfd);
#ifdef PS_SHARED_CAPTURE
 close(capture_broker);
#endif
 close(lock);return recovery?1:0;
}
