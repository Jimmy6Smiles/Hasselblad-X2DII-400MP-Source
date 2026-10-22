/* 自写私有采集适配器：固定原厂接口，不修改曝光档位。
 * AE 锁由上层 guard 持有；临时借用 Single 驱动模式及 Auto6，结束按日志恢复。
 * 六帧计数不是六文件所有权证据，更不是完整合成/发布验收。
 * 复用已经隔离测试的 AE 适配基础函数，保留原独立 CLI 以免改动已验收源码。 */
#define main ps_existing_ae_guard_cli
#include "exposure_guard.c"
#undef main
#include "format_guard.inc"
#include "initial_delay.h"

#ifndef AUTO6_METHOD_MS
#define AUTO6_METHOD_MS 45000
#endif
#ifndef AUTO6_DRAIN_MS
#define AUTO6_DRAIN_MS 120000
#endif
#ifndef AUTO6_LOCK_PATH
#define AUTO6_LOCK_PATH "/data/x2d2-integrated-v1/native-auto6.lock"
#endif
static const char *multi="multishot_control_mode";
static const char *modes[]={"E_MultiShotMode_SingleShot","E_MultiShotMode_Auto4",
 "E_MultiShotMode_Auto6","E_MultiShotMode_Auto16","E_MultiShotMode_Auto4B",
 "E_MultiShotMode_Auto6B","E_MultiShotMode_Auto16B"};
typedef struct { char magic[8],boot[40]; int32_t original,drive; uint32_t phase,check; } CaptureRecord;
_Static_assert(sizeof(CaptureRecord)==64,"capture journal ABI");
static uint32_t capture_hash(const CaptureRecord *r){
 const unsigned char *p=(const unsigned char*)r;uint32_t h=2166136261u;
 for(size_t i=0;i<sizeof(*r)-4;i++)h=(h^p[i])*16777619u;
 return h;
}
static int capture_save(int fd,CaptureRecord *r){
 r->check=capture_hash(r);size_t done=0;
 while(done<sizeof(*r)){ssize_t n=pwrite(fd,(char*)r+done,sizeof(*r)-done,done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  done+=(size_t)n;}
 return !ftruncate(fd,sizeof(*r))&&!fsync(fd);
}
static int get_multi(int32_t *v){
 char b[LIMIT],expected[128];if(!call(multi,NULL,b))return 0;
 for(int i=0;i<7;i++){snprintf(expected,sizeof expected,"%s = %s(%d)\n",multi,modes[i],i);
  if(!strcmp(b,expected)){*v=i;return 1;}}
 return 0;
}
static const char *drive_names[]={"Single","Continuous","SelfTimer","Interval","ExposureBracketing","FocusBracketing","SlowBurst"};
static int get_drive(int32_t *v){
 char b[LIMIT],expected[128];if(!call("drive_mode",NULL,b))return 0;
 for(int i=0;i<7;i++){snprintf(expected,sizeof expected,"drive_mode = E_DriveModes_%s(%d)\n",drive_names[i],i);
  if(!strcmp(b,expected)){*v=i;return 1;}}
 return 0;
}
static int set_drive(int32_t v){
 char b[LIMIT],value[80];int32_t actual;
 if(v<0||v>6)return 0;
 snprintf(value,sizeof value,"E_DriveModes_%s",drive_names[v]);
 return call("drive_mode",value,b)&&!b[0]&&get_drive(&actual)&&actual==v;
}
static int set_multi(int32_t v){
 char b[LIMIT];int32_t actual;return v>=0&&v<7&&call(multi,modes[v],b)&&!b[0]&&get_multi(&actual)&&actual==v;
}
static int ae_held(void){
 int32_t reset,user,enabled;
 return get(props[0],&reset)&&get(props[1],&user)&&get(props[2],&enabled)&&!reset&&user&&enabled;
}
static int drain(void){
 int64_t end=now()+AUTO6_DRAIN_MS;
 do {if(idle())return 1;struct timespec nap={0,200000000};nanosleep(&nap,NULL);}while(now()<end);
 return 0;
}
/* 单次方法，不复用两秒属性调用的超时；丢回包只报不确定，永不重发。 */
static int request_once(void){
 int fds[2];if(pipe2(fds,O_CLOEXEC))return 0;
 pid_t pid=fork();if(pid<0){close(fds[0]);close(fds[1]);return 0;}
 if(!pid){setpgid(0,0);close(fds[0]);dup2(fds[1],1);dup2(fds[1],2);close(fds[1]);
  execl(ODIN_PATH,ODIN_PATH,"-s","camera","-m","do_exposure_extended2","true",
   "E_SessionOptions_SimpleDriveMode|E_SessionOptions_SkipAutoFocus|E_SessionOptions_MultiShotExposure",
   "30000",(char*)NULL);_exit(127);}
 setpgid(pid,pid);close(fds[1]);fcntl(fds[0],F_SETFL,O_NONBLOCK);
 int status=0,finished=0,eof=0,ok=1;size_t used=0;char output[4096];int64_t end=now()+AUTO6_METHOD_MS;
 while(!finished||!eof){char b[256];ssize_t n=read(fds[0],b,sizeof b);
  if(n>0){if(used+(size_t)n>=sizeof output){ok=0;break;}memcpy(output+used,b,n);used+=n;}
  else if(!n)eof=1;else if(errno!=EAGAIN&&errno!=EINTR){ok=0;break;}
  if(!finished){pid_t w=waitpid(pid,&status,WNOHANG);if(w==pid)finished=1;else if(w<0&&errno!=EINTR){ok=0;break;}}
  if(finished&&eof)break;
  if(interrupted||now()>=end){ok=0;break;}
  struct pollfd p={fds[0],POLLIN,0};poll(&p,1,20);
 }
 if(!ok){kill(-pid,SIGKILL);kill(pid,SIGKILL);}
 if(!finished)while(waitpid(pid,&status,0)<0&&errno==EINTR){}
 close(fds[0]);output[used]=0;printf("METHOD_REPLY_BEGIN\n%s\nMETHOD_REPLY_END\n",output);
 return ok&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int capture_restore(int fd,CaptureRecord *r){
 int32_t v,drive;
 int borrowed=!memcmp(r->magic,"X2DCAP2",8);
 if(!idle()||!get_drive(&drive)||
    (drive!=r->drive&&(!borrowed||drive!=0))||!get_multi(&v))return 0;
 /* 不覆盖第三方新选项。恢复仅处理本适配器借用的 Auto6 或原值。 */
 if(v!=r->original&&(v!=2||!set_multi(r->original)))return 0;
 if(!get_multi(&v)||v!=r->original)return 0;
 if(drive!=r->drive&&!set_drive(r->drive))return 0;
 /* 驱动模式 setter 可能联动多拍选项；恢复后两者都需重新确认。 */
 if(!get_drive(&drive)||drive!=r->drive||!get_multi(&v)||v!=r->original)return 0;
 r->phase=3;return capture_save(fd,r);
}
/* Guarded capture configures Auto6 during the countdown, then performs the
 * exposure-sensitive work immediately before the one-shot request. */
#ifndef MODE_PREPARE_PATH
#define MODE_PREPARE_PATH "/data/x2d2-integrated-v1/mode-prepare.journal"
#endif
static int (*capture_ready)(void *);
static void *capture_ready_context;
static int64_t capture_not_before;
static int capture_cli(int argc,char **argv){
 if(argc!=3||(strcmp(argv[1],"--roundtrip")&&strcmp(argv[1],"--capture-once")&&strcmp(argv[1],"--recover")&&strcmp(argv[1],"--prepare-mode")&&strcmp(argv[1],"--release-mode"))){
  fputs("usage: native-auto6 --roundtrip|--capture-once|--recover /private/job/capture.journal\n",stderr);return 2;}
 if(argv[2][0]!='/'||strstr(argv[2],"/../")||strlen(argv[2])>4000)return 2;
 struct sigaction sa={0};sa.sa_handler=stop;sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
 int menu_release=!strcmp(argv[1],"--release-mode");if(menu_release&&strcmp(argv[2],MODE_PREPARE_PATH))return 2;int recovery=menu_release||!strcmp(argv[1],"--recover"),capture=!strcmp(argv[1],"--capture-once"),prepare=!strcmp(argv[1],"--prepare-mode"),setup_started=0;
 int fd=open(argv[2],O_RDWR|O_CLOEXEC|O_NOFOLLOW|(recovery?0:O_CREAT|O_EXCL),0600);
 if(fd<0){perror("capture journal");return 2;}
 struct stat st;int rc=1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return 2;}
 CaptureRecord r={0};
 if(recovery){char boot[40]={0};
  if(st.st_size!=sizeof r||pread(fd,&r,sizeof r,0)!=sizeof r||
   (memcmp(r.magic,"X2DCAP1",8)&&memcmp(r.magic,"X2DCAP2",8))||
   r.check!=capture_hash(&r)||!boot_id(boot)||(!menu_release&&memcmp(boot,r.boot,40))||r.original!=0||r.drive<0||r.drive>6||r.phase<1||r.phase>3)goto end;
  if(r.phase==3){puts("ALREADY_RESTORED_NO_CAPTURE_REPLAY");rc=0;goto end;}
  if(menu_release)memcpy(r.boot,boot,sizeof r.boot);
  rc=capture_restore(fd,&r)?0:4;puts(rc?"CAPTURE_RESTORE_PENDING":"CAPTURE_RECOVERED_NO_REPLAY");goto end;
 }
 int prewarmed=0;
 if(capture){
  int warm=open(MODE_PREPARE_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat ws;
  if(warm>=0){
   char boot[40]={0};int32_t drive=-1,multi_now=-1;
   int valid=!fstat(warm,&ws)&&S_ISREG(ws.st_mode)&&ws.st_uid==geteuid()&&ws.st_nlink==1&&!(ws.st_mode&0077)&&ws.st_size==sizeof r&&pread(warm,&r,sizeof r,0)==sizeof r;
   close(warm);
   if(!valid||memcmp(r.magic,"X2DCAP2",8)||r.check!=capture_hash(&r)||r.phase!=1||r.original!=0||r.drive<0||r.drive>6||!boot_id(boot)||memcmp(boot,r.boot,40)||!get_drive(&drive)||drive!=0||!get_multi(&multi_now)||multi_now!=2)goto end;
   if(!capture_save(fd,&r)||!parent_sync(argv[2]))goto end;
   /* Durable job record owns restoration before removing the menu record. */
   if(unlink(MODE_PREPARE_PATH)||!parent_sync(MODE_PREPARE_PATH))goto end;
   prewarmed=1;puts("MENU_PREPARE_CONSUMED");
  }else if(errno!=ENOENT)goto end;
 }
 if(!prewarmed){
 memcpy(r.magic,"X2DCAP2",8);r.phase=1;
 if(!boot_id(r.boot)||!idle()||!get_drive(&r.drive)||!get_multi(&r.original)||r.original!=0||
  (capture&&!capture_ready&&!ae_held())||interrupted||!capture_save(fd,&r)||!parent_sync(argv[2])){
  puts("PREFLIGHT_FAILED_NO_SETTINGS_WRITTEN");goto end;}
 }
 /* 在任何 setter 之前原模式已持久化；切换失败/丢包也走同一恢复路径。 */
 setup_started=1;
 int configured=prewarmed||((r.drive==0||set_drive(0))&&!interrupted&&set_multi(2)),ack=0,six=0;int32_t frames=-1,actual_drive=-1;
 if(prepare&&configured&&!interrupted){puts("MENU_PREPARE_READY_NO_AE_NO_CAPTURE_NO_FORMAT_CHANGE");rc=0;goto end;}
 if(configured&&capture&&capture_ready)configured=capture_ready(capture_ready_context);
 if(configured&&capture&&!interrupted&&idle()&&(capture_ready||ae_held())&&get_drive(&actual_drive)&&actual_drive==0){
  /* 持久化已消费标记在发送之前：崩溃恢复绝不重发拍摄。 */
  r.phase=2;
  if(capture_save(fd,&r)){
   while(!interrupted&&capture_not_before&&now()<capture_not_before){struct timespec nap={0,10000000};nanosleep(&nap,NULL);}
   if(interrupted)goto restore_capture;
   printf("PS_CAPTURE_TIME request_ms=%lld\n",(long long)now());puts("AUTO6_REQUEST_ONCE");fflush(stdout);ack=request_once();
   if(drain()&&get("exposures_in_multishot_session",&frames)&&frames==6)six=1;
   printf("AUTO6_OBSERVED_FRAMES=%d ACK=%d\n",frames,ack);}
 }
 restore_capture:;
 int canceled=interrupted;interrupted=0;
 if(!capture_restore(fd,&r)){puts("CAPTURE_RESTORE_PENDING");rc=4;}
 else {puts("MULTISHOT_SETTING_RESTORED");puts("DRIVE_MODE_RESTORED");rc=canceled?130:configured&&(!capture||(ack&&six))?0:1;}
end:close(fd);if(prepare&&!setup_started&&rc)unlink(argv[2]);return rc;
}
typedef struct {Record *ae;PsFormatRecord *format;int64_t until;} CountdownPrepare;
static int finish_countdown(void *context){
 CountdownPrepare *p=context;
 printf("PS_CAPTURE_TIME modes_prepared_ms=%lld deadline_ms=%lld\n",(long long)now(),(long long)p->until);fflush(stdout);
 /* Revalidate after waiting: external exposure changes must never be
  * overwritten silently. Only the mode setup is moved before the deadline. */

 int unchanged=!interrupted&&idle();int32_t actual=0;uint32_t format=0;
 /* Only the two writable AE flags need a pre-write ownership check.
  * Exposure selection is still checked AFTER locking by same_selection;
  * ae_lock_enabled is derived and is checked after the lock setter.
  * No cached menu-time exposure values are used. */
 for(unsigned i=0;unchanged&&i<2;i++)unchanged=get(props[i],&actual)&&actual==p->ae->original[i];
 unchanged=unchanged&&fmt_get(&format)&&format==p->format->original;
 if(!unchanged)return 0;
 int ready=!interrupted&&(p->format->original==0||fmt_set(0));int32_t enabled=0;
 /* Leave up to 900 ms of the requested countdown for AE lock/readback.
  * No exposure is allowed before the original shared deadline. */
 while(ready&&!interrupted&&now()+900<p->until){struct timespec nap={0,20000000};nanosleep(&nap,NULL);}
 int locked=ready&&!interrupted&&set_bool(0,0)&&!interrupted&&set_bool(1,1)&&get(props[2],&enabled)&&enabled==1&&same_selection(p->ae);
 printf("PS_CAPTURE_TIME ae_ready_ms=%lld\n",(long long)now());fflush(stdout);
 return locked;
}
/* 真正的机内调用链：延迟 -> AE guard -> 单次 Auto6 -> 还原。
 * 不启动 UI、不操作原片，后续 worker 必须另行验证六文件归属及曝光。 */
static int guarded(int argc,char **argv){
 int recovery=!strcmp(argv[1],"--recover-job");
 if((recovery&&argc!=3)||(!recovery&&argc!=4&&argc!=5))return 2;
 int delay=0;
 if(!recovery){char *end;long n=strtol(argv[3],&end,10);if(!*argv[3]||*end||n<2||n>60)return 2;delay=(int)n;}
 int64_t until=0;
 if(!recovery&&argc==5&&!ps_delay_parse(argv[4],now(),&until))return 2;
 char ae[4096],cap[4096],format[4096];struct stat st;
 if(argv[2][0]!='/'||strstr(argv[2],"/../")||strlen(argv[2])>3900)return 2;
 int dir=open(argv[2],O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(dir<0)return 2;
 int private_dir=!fstat(dir,&st)&&st.st_uid==geteuid()&&!(st.st_mode&0077);
#ifdef AUTO6_WINDOWS_HOST_TEST
 /* 仅主机 /mnt/c 的 DrvFS 不保存 chmod；ARM64 构建绝不定义此宏。
  * 主机测试不声称已验证真实目录权限，设备仍严格要求私有目录。 */
 private_dir=!fstat(dir,&st)&&st.st_uid==geteuid();
#endif
 close(dir);
 if(!private_dir)return 2;
 snprintf(ae,sizeof ae,"%s/ae.journal",argv[2]);snprintf(cap,sizeof cap,"%s/capture.journal",argv[2]);
 snprintf(format,sizeof format,"%s/format.journal",argv[2]);
 if(recovery){
  if(!lstat(cap,&st)){char *args[]={argv[0],"--recover",cap,NULL};if(capture_cli(3,args))return 4;}
  else if(errno!=ENOENT)return 4;
  PsFormatRecord fr;int ffd=fmt_open(format,&fr,1),format_ok=0;
  if(ffd>=0){format_ok=fr.phase==2||fmt_restore(ffd,&fr);close(ffd);}
  char *args[]={argv[0],"--recover",ae,NULL};int ae_result=ps_existing_ae_guard_cli(3,args);
  return format_ok?ae_result:4;
 }
 if(!lstat(cap,&st)||errno!=ENOENT)return 2;
 int fd=open(ae,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0)return 2;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return 2;}
 struct sigaction sa={0};sa.sa_handler=stop;sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
 printf("PS_CAPTURE_STAGE DELAY %d\n",delay);fflush(stdout);
 if(!until)until=now()+(int64_t)delay*1000;
 printf("PS_CAPTURE_DEADLINE %lld REMAINING %d\n",(long long)until,ps_delay_remaining(until,now()));fflush(stdout);
 /* 倒计时内仅查询和持久化恢复记录，绝不提前锁定测光或触发拍摄。 */
 Record r;int rc=1;
 /* Exit20 is reserved for a proven pre-write, pre-capture rejection. */
 if(interrupted||!snapshot(&r)||!save(fd,&r)||!parent_sync(ae)){puts("AE_PREFLIGHT_NO_WRITES");close(fd);return 20;}
 PsFormatRecord fr;int ffd=fmt_open(format,&fr,0);
 if(ffd<0){puts("FORMAT_PREFLIGHT_NO_WRITES");close(fd);return 20;}
 printf("PS_CAPTURE_FORMAT %u\n",fr.original);fflush(stdout);
 printf("PS_CAPTURE_TIME prepared_ms=%lld deadline_ms=%lld\n",(long long)now(),(long long)until);fflush(stdout);
 /* Auto6 setup now overlaps the countdown; AE is acquired by the callback. */
 /* 等待期间若有外部设置变化，拒绝本次请求；没有写过设置，不恢复旧值。 */
 printf("PS_CAPTURE_TIME ready_ms=%lld\n",(long long)now());fflush(stdout);
 if(!interrupted){
  CountdownPrepare prepared={&r,&fr,until};capture_ready=finish_countdown;capture_ready_context=&prepared;capture_not_before=until;
  char *args[]={argv[0],"--capture-once",cap,NULL};rc=capture_cli(3,args);
  capture_ready=NULL;capture_ready_context=NULL;
 }
 int cancelled=interrupted||rc==130;interrupted=0;
 int format_ok=fmt_restore(ffd,&fr);close(ffd);
 puts(format_ok?"PS_CAPTURE_STAGE FORMAT_RESTORED":"FORMAT_RESTORE_PENDING");
 /* Auto6 恢复未确认时不能直接释放 AE。 */
 if(rc==4||!restore(fd,&r)){puts("GUARDED_CAPTURE_RESTORE_PENDING");rc=4;}
 else {puts("PS_CAPTURE_STAGE AE_RESTORED");if(cancelled)rc=130;}
 if(!format_ok)rc=4;
 close(fd);return rc;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--check-readonly")){
  Record r;
  /* 不创建锁/日志、不设置 AE、不调用曝光方法。用于真实服务域的就绪检查。 */
  if(!snapshot(&r))return 1;
  puts("READONLY_AE_READY");return 0;
 }
 int lock_probe=argc==2&&!strcmp(argv[1],"--check-mode-lock");
 if(argc<3&&!lock_probe)return 2;
 int global=open(AUTO6_LOCK_PATH,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);struct stat gst;
 if(global<0){perror("mode preparation lock");return 2;}
 if(fstat(global,&gst)||!S_ISREG(gst.st_mode)||gst.st_uid!=geteuid()||gst.st_nlink!=1||flock(global,LOCK_EX|LOCK_NB)){close(global);return 2;}
 int rc=lock_probe?0:(!strcmp(argv[1],"--guarded-capture")||!strcmp(argv[1],"--recover-job"))?guarded(argc,argv):capture_cli(argc,argv);
 if(lock_probe&&!rc)puts("MODE_LOCK_READY_NO_CAMERA_SETTINGS");
 close(global);return rc;
}
