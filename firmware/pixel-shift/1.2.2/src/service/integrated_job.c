/* 自写机内串联器：原生 Auto6 -> 仅本次新增六 RAW -> 合成 -> 所选格式封装。
 * 清理须核对本次六帧及发布成片的全文件摘要；目录差分不是唯一删除凭据。
 * 每个阶段先落持久日志；不重拍，不覆盖成片；不支持 HEIF 时在拍摄前拒绝。 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef PS_BIN
#define PS_BIN "/dev/x2d2-integrated-v1"
#endif
#ifndef PS_DATA
#define PS_DATA "/data/x2d2-integrated-v1"
#endif
#ifndef PS_MEDIA
#define PS_MEDIA "/mnt/media_rw"
#endif
#ifndef PS_ODIN
#define PS_ODIN "/system/bin/odindb-send"
#endif
#define COUNT 4096
#ifndef PS_HASH_TOOL
#define PS_HASH_TOOL "/system/bin/toybox"
#endif
typedef struct {char name[32];dev_t dev;ino_t ino;off_t size;struct timespec mt;} Entry;
static volatile sig_atomic_t stopping;
static void sigstop(int n){(void)n;stopping=1;}
static int64_t clock_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int join(char *out,size_t cap,const char *a,const char *b){int n=snprintf(out,cap,"%s/%s",a,b);return n>0&&(size_t)n<cap;}
static int token(const char *s){if(!s||!*s||*s=='0'||strlen(s)>18)return 0;for(;*s;s++)if(*s<'0'||*s>'9')return 0;return 1;}
static int sync_dir(const char *p){int fd=open(p,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;int ok=!fsync(fd);close(fd);return ok;}
static int regular(const char *p,struct stat *st){return !lstat(p,st)&&S_ISREG(st->st_mode)&&st->st_nlink==1;}
static int run_completed,run_exit_code,run_cancel_confirmed;
/* 固定 argv、输出有界、独立进程组；超时/取消后不重复执行。 */
static int run(char *const av[],char *reply,size_t cap,int timeout_ms,int logfd){
 run_completed=0;run_exit_code=-1;run_cancel_confirmed=0;if(reply&&cap)reply[0]=0;
 int pp[2];if(pipe2(pp,O_CLOEXEC|O_NONBLOCK))return 0;
 pid_t pid=fork();if(pid<0){close(pp[0]);close(pp[1]);return 0;}
 if(!pid){setpgid(0,0);close(pp[0]);fcntl(pp[1],F_SETFL,0);dup2(pp[1],1);dup2(pp[1],2);close(pp[1]);execv(av[0],av);_exit(127);}
 setpgid(pid,pid);close(pp[1]);size_t used=0;int status=0,done=0,term=0,overflow=0;int64_t start=clock_ms(),halt=0;
 while(!done){
  char buf[1024];ssize_t n;
  while((n=read(pp[0],buf,sizeof buf))>0){
   if(logfd>=0&&write(logfd,buf,(size_t)n)!=n)overflow=1;
   if(reply){if((size_t)n>=cap-used)overflow=1;else{memcpy(reply+used,buf,(size_t)n);used+=(size_t)n;}}
  }
  pid_t got=waitpid(pid,&status,WNOHANG);if(got==pid){done=1;break;}if(got<0&&errno!=EINTR){close(pp[0]);return 0;}
  int64_t now=clock_ms();
  if(!term&&(stopping||overflow||now<start||now-start>=timeout_ms)){kill(-pid,SIGTERM);term=1;halt=now;}
  if(term&&now-halt>=30000){kill(-pid,SIGKILL);if(now-halt>=32000){close(pp[0]);return 0;}}
  struct pollfd p={pp[0],POLLIN,0};poll(&p,1,20);
 }
 /* 子进程退出后还可能有尾部管道数据。 */
 char tail[1024];ssize_t n;while((n=read(pp[0],tail,sizeof tail))>0){
  if(logfd>=0&&write(logfd,tail,(size_t)n)!=n)overflow=1;
  if(reply){if((size_t)n>=cap-used)overflow=1;else{memcpy(reply+used,tail,(size_t)n);used+=(size_t)n;}}
 }
 close(pp[0]);if(reply)reply[used]=0;
 run_completed=done&&!term&&!overflow&&WIFEXITED(status);if(run_completed)run_exit_code=WEXITSTATUS(status);
 run_cancel_confirmed=done&&stopping&&!overflow&&WIFEXITED(status)&&WEXITSTATUS(status)==130;
 return done&&!term&&!overflow&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int prop(const char *service,const char *name,char *out,size_t cap){
 char text[512],prefix[80];char *av[]={PS_ODIN,"-s",(char*)service,"-p",(char*)name,NULL};
 if(!run(av,text,sizeof text,3000,-1))return 0;
 int n=snprintf(prefix,sizeof prefix,"%s = ",name);if(n<0||(size_t)n>=sizeof prefix||strncmp(text,prefix,(size_t)n))return 0;
 size_t len=strlen(text+n);if(len&&text[n+len-1]=='\n')len--;if(!len||len>=cap||memchr(text+n,'\n',len)||memchr(text+n,'\r',len))return 0;
 memcpy(out,text+n,len);out[len]=0;return 1;
}
typedef struct {char media[4],folder[12];unsigned format;} Settings;
/* 成片与六帧使用同一个拍摄开始时选定的默认目录；不建固定合成相册。 */
static int selected_album(char *out,size_t cap,const Settings *v){
 if((strcmp(v->media,"ssd")&&strcmp(v->media,"cfe"))||strlen(v->folder)!=8||
    v->folder[0]<'1'||v->folder[0]>'9'||v->folder[1]<'0'||v->folder[1]>'9'||
    v->folder[2]<'0'||v->folder[2]>'9'||strcmp(v->folder+3,"HASBL"))return 0;
 int n=snprintf(out,cap,PS_MEDIA"/%s/DCIM/%s",v->media,v->folder);return n>0&&(size_t)n<cap;
}
static int settings(Settings *v){
 char p[128],f[128],mode[128],format[128];
 if(!prop("storage","primary_device",p,sizeof p)||!prop("storage","capture_storage_mode",mode,sizeof mode)||
    strcmp(mode,"E_CaptureStorageMode_LocalStorage(1)")||!prop("storage","save_folder",f,sizeof f)||
    !prop("camera","image_format",format,sizeof format))return 0;
 if(!strcmp(p,"E_StorageDevice_InternalFlash(0)"))strcpy(v->media,"ssd");
 else if(!strcmp(p,"E_StorageDevice_CfExpressCard(1)"))strcpy(v->media,"cfe");else return 0;
 if(strlen(f)!=13||f[0]!='/'||strncmp(f+1,v->media,3)||f[4]!='/'||f[5]<'1'||f[5]>'9'||f[6]<'0'||f[6]>'9'||f[7]<'0'||f[7]>'9'||strcmp(f+8,"HASBL"))return 0;
 strcpy(v->folder,f+5);
 const char *formats[]={"E_ImageFormat_Raw(0)","E_ImageFormat_RawJpeg(1)","E_ImageFormat_Jpeg(2)"};
 for(unsigned i=0;i<3;i++)if(!strcmp(format,formats[i])){v->format=i;return 1;}return 0;
}
static int entry_name(const char *s){
 if(strlen(s)!=12||s[0]!='B')return 0;for(int i=1;i<8;i++)if(s[i]<'0'||s[i]>'9')return 0;
 return !strcmp(s+8,".3FR")||!strcmp(s+8,".JPG")||!strcmp(s+8,".HIF");
}
static int compare(const void *a,const void *b){return strcmp(((const Entry*)a)->name,((const Entry*)b)->name);}
static int scan(const char *path,Entry *v,int *count){
 DIR *d=opendir(path);if(!d)return 0;int n=0,ok=1;struct dirent *de;
 while((de=readdir(d))){
  if(!entry_name(de->d_name))continue;
  struct stat st;if(n==COUNT||fstatat(dirfd(d),de->d_name,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode)||st.st_nlink!=1){ok=0;break;}
  strcpy(v[n].name,de->d_name);v[n].dev=st.st_dev;v[n].ino=st.st_ino;v[n].size=st.st_size;v[n].mt=st.st_mtim;n++;
 }
 closedir(d);qsort(v,(size_t)n,sizeof *v,compare);*count=n;return ok;
}
/* 卡上 inode 会重分配：这里只判断目录清单连续性，不作为删除授权。
 * 新六帧仍须挂载代次固定、整文件摘要前后相同及 worker 六帧元数据检查。 */
static int identical(const Entry *a,const Entry *b){return a->dev==b->dev&&a->size==b->size&&a->mt.tv_sec==b->mt.tv_sec&&a->mt.tv_nsec==b->mt.tv_nsec;}
static int new_six(Entry *before,int nb,Entry *after,int na,Entry six[6]){
 int n=0;
 for(int i=0;i<nb;i++){Entry *p=bsearch(before+i,after,(size_t)na,sizeof *after,compare);if(!p||!identical(p,before+i))return 0;}
 for(int i=0;i<na;i++)if(!bsearch(after+i,before,(size_t)nb,sizeof *before,compare)){
  if(n==6||strcmp(after[i].name+8,".3FR")||after[i].size<=210510336)return 0;six[n++]=after[i];
 }
 if(n!=6)return 0;
 for(int i=1;i<6;i++)if(strtol(six[i].name+1,NULL,10)!=strtol(six[0].name+1,NULL,10)+i)return 0;
 return 1;
}
static int checkpoint(int fd,const char *stage){return dprintf(fd,"%lld %s\n",(long long)clock_ms(),stage)>0&&!fsync(fd);}
typedef struct {dev_t dev;char mount[1024];} VolumeIdentity;
static int volume_read(const char *path,VolumeIdentity *v){
 struct stat st;if(lstat(path,&st)||!S_ISDIR(st.st_mode))return 0;
 FILE *f=fopen("/proc/self/mountinfo","r");if(!f)return 0;
 char line[1024],point[1024];int found=0;
 while(fgets(line,sizeof line,f)){
  if(!strchr(line,'\n')){int ch;while((ch=fgetc(f))!=EOF&&ch!='\n'){}continue;}
  if(sscanf(line,"%*u %*u %*s %*s %1023s",point)==1&&!strcmp(point,path)){
   v->dev=st.st_dev;strcpy(v->mount,line);found=1;break;
  }
 }
 fclose(f);return found;
}
static int volume_same(const char *path,const VolumeIdentity *old){VolumeIdentity v;return volume_read(path,&v)&&v.dev==old->dev&&!strcmp(v.mount,old->mount);}
static int source_hash(const char *path,char out[65]){
 struct stat before,after;char reply[640];
 if(!regular(path,&before))return 0;
 char *av[]={PS_HASH_TOOL,"sha256sum",(char*)path,NULL};
 if(!run(av,reply,sizeof reply,90000,-1)||strlen(reply)<66||reply[64]!=' ')return 0;
 for(int i=0;i<64;i++)if(!((reply[i]>='0'&&reply[i]<='9')||(reply[i]>='a'&&reply[i]<='f')))return 0;
 if(!regular(path,&after)||before.st_dev!=after.st_dev||before.st_size!=after.st_size||
    before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)return 0;
 memcpy(out,reply,64);out[64]=0;return 1;
}
static int exclusive_rename(const char *from,const char *to){
#ifdef PS_HOST_TEST_NONATOMIC_RENAME
 /* 仅 DrvFS 主机 IPC 夹具：不验证原子防覆盖。生产 ARM64 禁止定义此宏。 */
 struct stat s;if(!lstat(to,&s)||errno!=ENOENT)return 0;return !rename(from,to);
#else
 return !syscall(SYS_renameat2,AT_FDCWD,from,AT_FDCWD,to,1);
#endif
}
#include "album_job_client.inc"
/* 选择整组未占用编号；最终提交仍用 RENAME_NOREPLACE 防止检查后竞态。 */
static int output_stem(const char *album,const char *first,char stem[9]){
 if(strlen(first)!=8||first[0]!='B')return 0;
 for(int i=1;i<8;i++)if(first[i]<'0'||first[i]>'9')return 0;
 unsigned start=(unsigned)strtoul(first+1,NULL,10);
 const char *ext[]={"3FR","JPG","JPEG","HEIF","HEIC","DNG"};
 for(unsigned n=start;n<=9999999&&n-start<10000;n++){
  int occupied=0;char p[512];struct stat st;
  for(unsigned i=0;i<sizeof ext/sizeof *ext;i++){
   int k=snprintf(p,sizeof p,"%s/B%07u.%s",album,n,ext[i]);
   if(k<0||(size_t)k>=sizeof p)return 0;
   if(!lstat(p,&st)){occupied=1;break;}
   if(errno!=ENOENT)return 0;
  }
  if(!occupied){snprintf(stem,9,"B%07u",n);return 1;}
 }
 return 0;
}
/* 仅内部调用；原厂 remove 同步索引，禁止直接搬走/删除文件留下幽灵相册项。 */
static int cleanup_six(const char *volume,const VolumeIdentity *vol,const char *source,
 const Entry six[6],char hashes[6][65],const Settings *v,
 const char *stem,char output_hashes[2][65],int journal);
#include "album_native_cleanup.inc"
#include "initial_delay.h"
static int job(const char *id,const char *delay,const char *deadline){
 if(!token(id)||!token(delay)||strtol(delay,NULL,10)<2||strtol(delay,NULL,10)>60)return 2;
 int64_t until=0;if(deadline&&!ps_delay_parse(deadline,clock_ms(),&until))return 2;
 char journal[256],work[256],volume[256],source[256],stage[256],album[256],p[512],q[512],dng[512],stem[9];
 Settings a={0},b={0};VolumeIdentity vol;struct stat st;struct statvfs space;int log=-1,jfd=-1,result=1,nb=0,na=0;char hashes[6][65],output_hashes[2][65];
 int capture_restored=0,publish_started=0;
 Entry *before=calloc(COUNT,sizeof *before),*after=calloc(COUNT,sizeof *after),six[6];
 if(!before||!after||!settings(&a)||a.format!=0)goto done;
 snprintf(journal,sizeof journal,PS_DATA"/jobs/%s",id);snprintf(work,sizeof work,PS_BIN"/jobs/%s",id);
 snprintf(volume,sizeof volume,PS_MEDIA"/%s",a.media);
 if(!selected_album(source,sizeof source,&a)||!selected_album(album,sizeof album,&a))goto done;
 snprintf(stage,sizeof stage,"%s/.x2d2-pixelshift/%s",volume,id);
 if(!volume_read(volume,&vol)||statvfs(volume,&space)||(uint64_t)space.f_bavail*space.f_frsize<UINT64_C(6)*1024*1024*1024)goto done;
 /* 父级仅由安装器创建；任务目录必须全新，拒绝重放。 */
 if(mkdir(journal,0700)||mkdir(work,0700)||mkdir(stage,0700))goto done;
 join(p,sizeof p,journal,"pipeline.log");jfd=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(jfd<0)goto done;
 join(p,sizeof p,journal,"children.log");log=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(log<0)goto done;
 if(!checkpoint(jfd,"PREPARE_NO_DELETE")||!sync_dir(journal)||!scan(source,before,&nb))goto done;
 join(p,sizeof p,journal,"before.bin");int fd=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0)goto done;int saved=write(fd,before,(size_t)nb*sizeof *before)==(ssize_t)((size_t)nb*sizeof *before)&&!fsync(fd);close(fd);if(!saved)goto done;
 if(!album_refresh_folder(&a,jfd)||!checkpoint(jfd,"CAPTURE_INTENT_ONCE"))goto done;
 char *capture[]={PS_BIN"/native-auto6","--guarded-capture",journal,(char*)delay,(char*)deadline,NULL};
 if(!run(capture,NULL,0,240000,log)){
  if(run_cancel_confirmed){capture_restored=1;checkpoint(jfd,"CAPTURE_CANCEL_RESTORED");}
  else checkpoint(jfd,"CAPTURE_FAILED_RETAIN_INPUTS_RECOVERY_REQUIRED");
  goto done;
 }
 capture_restored=1;
 if(stopping||!volume_same(volume,&vol)||!settings(&b)||memcmp(&a,&b,sizeof a)||!scan(source,after,&na)||!new_six(before,nb,after,na,six))goto done;
 if(!checkpoint(jfd,"CAPTURE_SIX_VERIFIED"))goto done;
 for(int i=0;i<6;i++){
  char name[32];snprintf(name,sizeof name,"input%d.3fr",i);join(p,sizeof p,work,name);join(q,sizeof q,source,six[i].name);
  if(!volume_same(volume,&vol)||!source_hash(q,hashes[i]))goto done;
  if(dprintf(jfd,"INPUT_SHA256 %s %s\n",six[i].name,hashes[i])<0||fsync(jfd))goto done;
  if(symlink(q,p))goto done;
  if(dprintf(jfd,"INPUT_RETAIN %s %llu %llu %lld\n",q,(unsigned long long)six[i].dev,(unsigned long long)six[i].ino,(long long)six[i].size)<0)goto done;
 }
 memcpy(stem,six[0].name,8);stem[8]=0;
 join(dng,sizeof dng,stage,"merged.dng");if(!checkpoint(jfd,"MERGE"))goto done;
 char *merge[]={PS_BIN"/worker","--auto-six",work,dng,NULL};if(!run(merge,NULL,0,300000,log)||!regular(dng,&st)||st.st_size<=815010840)goto done;
 if(!volume_same(volume,&vol)||stopping)goto done;
 if(a.format!=2){
  join(p,sizeof p,stage,"output.3FR");
 /* 已验收的 Phocus 兼容封装必须使用本组六帧的首帧元数据。 */
  char first_raw[512];if(!join(first_raw,sizeof first_raw,source,six[0].name))goto done;
  char *pack[]={PS_BIN"/raw-pack",dng,p,first_raw,NULL};
  if(!checkpoint(jfd,"RAW_PACK")||!run(pack,NULL,0,240000,log)||!regular(p,&st)||st.st_size<=815010840)goto done;
 }
 if(a.format!=0){
  join(p,sizeof p,stage,"output.JPG");char *jpeg[]={PS_BIN"/full-jpeg",dng,p,NULL};
  if(!checkpoint(jfd,"JPEG_ENCODE")||!run(jpeg,NULL,0,600000,log)||!regular(p,&st)||st.st_size<=1024)goto done;
 }
 if(stopping||!volume_same(volume,&vol)||!settings(&b)||memcmp(&a,&b,sizeof a))goto done;
 for(int i=0;i<6;i++){
  char digest[65];join(q,sizeof q,source,six[i].name);
  if(!source_hash(q,digest)||strcmp(digest,hashes[i])||!volume_same(volume,&vol))goto done;
 }
 /* 默认目录须仍为已核对的原目录，不在结束时悄悄创建或换目录。 */
 if(lstat(album,&st)||!S_ISDIR(st.st_mode)||st.st_dev!=vol.dev)goto done;
 char preferred[9];memcpy(preferred,stem,sizeof preferred);
 if(!output_stem(album,preferred,stem)||dprintf(jfd,"OUTPUT_STEM %s FIRST_INPUT %s\n",stem,preferred)<0||fsync(jfd))goto done;
 const char *ext[]={"3FR","JPG"};
 for(int i=0;i<2;i++)if((i==0&&a.format!=2)||(i==1&&a.format!=0)){
  snprintf(p,sizeof p,"%s/output.%s",stage,ext[i]);if(!source_hash(p,output_hashes[i]))goto done;
  snprintf(q,sizeof q,"%s/%s.%s",album,stem,ext[i]);if(!lstat(q,&st)||errno!=ENOENT)goto done;
 }
 /* 成对输出不是文件系统原子事务；记录已提交部分且保留全部输入，不自动重试。 */
 if(!checkpoint(jfd,"PUBLISH_INTENT_RETAIN_SIX"))goto done;
 publish_started=1;
 for(int i=0;i<2;i++)if((i==0&&a.format!=2)||(i==1&&a.format!=0)){
  snprintf(p,sizeof p,"%s/output.%s",stage,ext[i]);snprintf(q,sizeof q,"%s/%s.%s",album,stem,ext[i]);
  if(!volume_same(volume,&vol)||!exclusive_rename(p,q)||!sync_dir(album)||dprintf(jfd,"PUBLISHED %s\n",q)<0||fsync(jfd))goto done;
 }
 if(!checkpoint(jfd,"FILES_PUBLISHED_BEFORE_INPUT_CLEANUP"))goto done;
 if(!album_commit_verified(&a,stem,output_hashes,jfd))goto done;
#ifdef PS_PHOCUS_TRIAL
 if(!checkpoint(jfd,"PHOCUS_TRIAL_OUTPUT_REGISTERED_INPUTS_RETAINED"))goto done;
#else
 if(!cleanup_six(volume,&vol,source,six,hashes,&a,stem,output_hashes,jfd))goto done;
#endif
 result=0;
done:
 /* 只将原厂采集已正常恢复、尚未发布成片的取消确认成终态。
  * 保留原片及证据；发布/删除中断或采集恢复不明确仍走人工核对。 */
 if(result&&stopping&&capture_restored&&!publish_started&&jfd>=0){
  char exposure[96],processing[96];stopping=0;
  int safe=prop("camera","exposure_status",exposure,sizeof exposure)&&!strcmp(exposure,"E_ExposureStatus_None(0)")&&
   prop("camera","exposures_processing_counter",processing,sizeof processing)&&!strcmp(processing,"0")&&
   settings(&b)&&!memcmp(&a,&b,sizeof a)&&!stopping;
  stopping=1;
  if(safe&&checkpoint(jfd,"CANCELLED_IDLE_INPUTS_RETAINED"))result=130;
 }
 if(result&&result!=130&&jfd>=0)checkpoint(jfd,"FAILED_NO_AUTOMATIC_REPLAY_REVIEW_CLEANUP_JOURNAL");
 if(log>=0){fsync(log);close(log);}if(jfd>=0)close(jfd);free(before);free(after);return result;
}
#ifndef PS_JOB_TEST
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--preflight")){
  Settings v={0};if(!settings(&v)||v.format!=0||!album_runtime_ready())return 1;
  char p[256];snprintf(p,sizeof p,PS_MEDIA"/%s",v.media);struct statvfs space;
  if(statvfs(p,&space)||(uint64_t)space.f_bavail*space.f_frsize<UINT64_C(6)*1024*1024*1024)return 1;
  printf("READY format=%u media=%s folder=%s\n",v.format,v.media,v.folder);return 0;
 }
 if((argc!=4&&argc!=5)||strcmp(argv[1],"--run-once"))return 2;
 signal(SIGTERM,sigstop);signal(SIGINT,sigstop);
 int fd=open(PS_DATA"/pipeline.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);struct stat s;
 if(fd<0||fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_uid!=getuid()||s.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB))return 2;
 int result=job(argv[2],argv[3],argc==5?argv[4]:NULL);close(fd);return result;
}
#endif
