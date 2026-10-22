/* 自写任务端：显式刷新只是请求凭据，必须另向原厂逐字段核对成片。
 * 当前设备证据仅覆盖 RAW；配对 JPEG 的原厂索引语义未验证，不提前放行。 */
#include "album_refresh_contract.h"
#ifndef PS_ALBUM_REFRESH_ROOT
#define PS_ALBUM_REFRESH_ROOT "/dev/x2d2-album-refresh-v1"
#endif
#ifndef PS_ALBUM_COMMAND
#define PS_ALBUM_COMMAND run
#endif
#include "album_runtime.h"
static int album_field(const char *text,const char *key,const char *value){
 char prefix[80];int n=snprintf(prefix,sizeof prefix,"   %s = ",key);
 if(n<=0||(size_t)n>=sizeof prefix||!text||strncmp(text,"metadata {\n",11))return 0;
 unsigned found=0;const char *p=text;
 while(*p){const char *end=strchr(p,'\n');if(!end)return 0;
  size_t len=(size_t)(end-p);
  if(len>=(size_t)n&&!memcmp(p,prefix,(size_t)n)){
   if(len!=(size_t)n+strlen(value)||memcmp(p+n,value,strlen(value)))return 0;found++;
  }
  p=end+1;
 }
 return found==1;
}
static int album_names(const Settings *v,const char *stem,char folder[14],char display[32],char physical[256]){
 char file[16];int n=snprintf(file,sizeof file,"%s.3FR",stem);
 if(n!=12||!entry_name(file)||!selected_album(physical,256,v))return 0;
 n=snprintf(folder,14,"/%s/%s",v->media,v->folder);if(n!=13)return 0;
 n=snprintf(display,32,"%s/%s",folder,stem);return n==22;
}
static int album_metadata(const char *display,const char *physical,off_t size){
 char reply[8192],bytes[32];char *av[]={PS_ODIN,"-s","storage","-m","get_metadata",(char*)display,"E_MetadataOptions_FileInfo",NULL};
 if(size<=0||!PS_ALBUM_COMMAND(av,reply,sizeof reply,6000,-1))return 0;
 snprintf(bytes,sizeof bytes,"%lld",(long long)size);
 return album_field(reply,"display_path",display)&&album_field(reply,"full_path",physical)&&
  album_field(reply,"size",bytes)&&album_field(reply,"persistent","true")&&
  album_field(reply,"type","1")&&album_field(reply,"imageFormat","0");
}
static int album_absent(const char *display){
 char reply[8192],expected[128];char *av[]={PS_ODIN,"-s","storage","-m","get_metadata",(char*)display,"E_MetadataOptions_FileInfo",NULL};
 int ok=PS_ALBUM_COMMAND(av,reply,sizeof reply,6000,-1);
 snprintf(expected,sizeof expected,"Failed to lookup item \"%s\"",display);
 return !ok&&run_completed&&run_exit_code==1&&strstr(reply,expected)&&!strstr(reply,"metadata {");
}
static int album_refresh_folder(const Settings *v,int journal){
 char folder[14],display[32],physical[256];
 if(stopping||v->format!=0||!album_names(v,"B0000001",folder,display,physical))return 0;
 struct stat st;if(lstat(PS_ALBUM_REFRESH_ROOT,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid())return 0;
 struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;
 uint64_t tick=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
 PsAlbumRefresh r={PS_ALBUM_REFRESH_MAGIC,(uint64_t)ts.tv_sec*1000000000+ts.tv_nsec,tick+60000,{0},0};
 memcpy(r.folder,folder,14);
 char pending[256],result[256],expected[80],reply[8192];
 char *av[]={PS_ODIN,"-s","storage","-m","browse",folder,"E_MetadataOptions_None",NULL};
 /* 新开机未浏览的目录先走普通浏览；显式刷新要求已有原厂 CacheDir。 */
 /* browse 可能返回整个目录；无需解析列表，持续排空管道并检查退出状态。
  * 成片仍由下方独立 get_metadata 与摘要逐项核验，不放宽其缓冲边界。 */
 if(!PS_ALBUM_COMMAND(av,NULL,0,6000,-1))return 0;
 snprintf(pending,sizeof pending,PS_ALBUM_REFRESH_ROOT"/request-%016llx.tmp",(unsigned long long)r.token);
 snprintf(result,sizeof result,PS_ALBUM_REFRESH_ROOT"/request-%016llx.result",(unsigned long long)r.token);
 /* 先持久记录意图，再原子发布完整请求；失败遗留用于诊断，不覆盖旧请求。 */
 if(journal>=0&&(dprintf(journal,"ALBUM_REFRESH_TOKEN %llu %s\n",(unsigned long long)r.token,folder)<0||fsync(journal)))return 0;
 int fd=open(pending,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 int ok=write(fd,&r,sizeof r)==sizeof r&&!fsync(fd);close(fd);
 if(!ok||!exclusive_rename(pending,PS_ALBUM_REFRESH_ROOT"/refresh.request"))return 0;
 if(!PS_ALBUM_COMMAND(av,NULL,0,6000,-1))return 0;
 fd=open(result,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&st.st_size>0&&st.st_size<80)n=read(fd,reply,80);
 close(fd);if(n<=0||n>=80)return 0;reply[n]=0;
 snprintf(expected,sizeof expected,"RESCAN_REQUESTED %llu\n",(unsigned long long)r.token);
 return !strcmp(reply,expected); /* 仍然不是成片确认。 */
}
static int album_commit_verified(const Settings *v,const char *stem,char output_hashes[2][65],int journal){
 char folder[14],display[32],album[256],path[512],digest[65];struct stat st;
 if(journal<0||v->format!=0||!album_names(v,stem,folder,display,album))return 0;
 snprintf(path,sizeof path,"%s/%s.3FR",album,stem);
 if(!regular(path,&st)||!source_hash(path,digest)||strcmp(digest,output_hashes[0])||!album_refresh_folder(v,journal))goto failed;
 for(int i=0;i<10&&!stopping;i++){
  if(album_metadata(display,path,st.st_size)){
   if(!source_hash(path,digest)||strcmp(digest,output_hashes[0]))goto failed;
   return checkpoint(journal,"ALBUM_REGISTERED_OUTPUT_VERIFIED");
  }
  struct timespec wait={0,200000000};nanosleep(&wait,NULL);
 }
failed:
 checkpoint(journal,"ALBUM_IMPORT_FAILED_RETAIN_SIX");return 0;
}
