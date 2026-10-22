/* 实际任务 C 代码 + 假原厂 CLI；仅删除专用输出目录里的自写文本夹具。 */
#define PS_JOB_TEST
#define PS_HASH_TOOL "/usr/bin/env"
#define PS_ALBUM_COMMAND fake_odin
#define PS_MEDIA "media"
#define PS_ALBUM_REFRESH_ROOT "refresh"
#define PS_HOST_TEST_NONATOMIC_RENAME
#include <stddef.h>
static int fake_odin(char *const[],char*,size_t,int,int);
#include "integrated_job.c"
#include <assert.h>
static int scenario,removes,checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void fixture(const char *path,const char *data){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);CHECK(write(fd,data,strlen(data))==(ssize_t)strlen(data));close(fd);
}
static int fake_odin(char *const av[],char *reply,size_t cap,int timeout,int log){
 (void)timeout;(void)log;run_completed=1;run_exit_code=0;if(reply&&cap)reply[0]=0;
 if(!strcmp(av[4],"browse")){
  CHECK(reply==NULL&&cap==0);
  CHECK(!strcmp(av[6],"E_MetadataOptions_None"));
  PsAlbumRefresh r;int fd=open("refresh/refresh.request",O_RDONLY);
  if(fd<0)return 1;CHECK(read(fd,&r,sizeof r)==sizeof r);close(fd);
  CHECK(ps_album_refresh_valid(&r,av[5],(uint64_t)clock_ms()));
  char name[128],text[80];snprintf(name,sizeof name,"refresh/request-%016llx.claimed",(unsigned long long)r.token);
  CHECK(!rename("refresh/refresh.request",name));
  snprintf(name,sizeof name,"refresh/request-%016llx.result",(unsigned long long)r.token);
  snprintf(text,sizeof text,"RESCAN_REQUESTED %llu\n",(unsigned long long)r.token+(scenario==10));fixture(name,text);return 1;
 }
 char p[256];CHECK(!strncmp(av[5],"/cfe/101HASBL/B",14));
 const char *stem=strrchr(av[5],'/')+1;snprintf(p,sizeof p,"media/cfe/DCIM/101HASBL/%s.3FR",stem);
 struct stat st;
 if(!strcmp(av[4],"remove")){
  removes++;
  if(scenario==4&&removes==3){run_completed=0;run_exit_code=-1;return 0;}
  CHECK(!unlink(p));strcpy(reply,"Ok\n");return 1;
 }
 CHECK(!strcmp(av[4],"get_metadata"));
 if(lstat(p,&st)){
  if(scenario==5){run_exit_code=1;strcpy(reply,"DBus disconnected\n");return 0;}
  if(scenario==6){run_completed=0;snprintf(reply,cap,"Failed to lookup item \"%s\"\n",av[5]);return 0;}
  if(scenario==7){run_exit_code=1;strcpy(reply,"Failed to lookup item \"/cfe/101HASBL/B9999999\"\n");return 0;}
  if(scenario!=8){run_exit_code=1;snprintf(reply,cap,"Failed to lookup item \"%s\"\n",av[5]);return 0;}
  st.st_size=16; /* 模拟文件已删但索引仍残留。 */
 }
 snprintf(reply,cap,"metadata {\n   display_path = %s\n   full_path = %s\n   size = %lld\n   persistent = true\n   type = 1\n   imageFormat = 0\n}\n",av[5],p,(long long)st.st_size);
 if(scenario==9&&strstr(stem,"0000106"))strcpy(reply,"metadata {\n   persistent = true\n}\n");
 return 1;
}
int main(int argc,char **argv){
 CHECK(argc==2);VolumeIdentity vol;CHECK(volume_read("/mnt/d",&vol));
 char root[600];snprintf(root,sizeof root,"%s/album-client-XXXXXX",argv[1]);CHECK(mkdtemp(root));CHECK(!chdir(root));
 CHECK(album_field("metadata {\n   x = yes\n}\n","x","yes"));
 CHECK(!album_field("metadata {\n   x = yes\n   x = yes\n}\n","x","yes"));
 CHECK(!album_field("metadata {\n   x = yesMORE\n}\n","x","yes"));
 CHECK(!album_field("metadata {\n   x = yes","x","yes"));
 for(scenario=0;scenario<12;scenario++){identity_reset();
  char sub[32];snprintf(sub,sizeof sub,"case-%d",scenario);CHECK(!mkdir(sub,0700));CHECK(!chdir(sub));
  CHECK(!mkdir("refresh",0700));CHECK(!mkdir("media",0700));CHECK(!mkdir("media/cfe",0700));CHECK(!mkdir("media/cfe/DCIM",0700));CHECK(!mkdir("media/cfe/DCIM/101HASBL",0700));
  const char *album="media/cfe/DCIM/101HASBL";char p[256],hashes[6][65],outs[2][65];Entry six[6]={0};Settings v={.media="cfe",.folder="101HASBL"};removes=0;
  for(int i=0;i<6;i++){
   snprintf(six[i].name,sizeof six[i].name,"B%07d.3FR",100+i);snprintf(p,sizeof p,"%s/%s",album,six[i].name);fixture(p,"test raw fixture");
   struct stat st;CHECK(!lstat(p,&st));six[i].size=st.st_size;CHECK(source_identity(p,hashes[i]));
  }
  snprintf(p,sizeof p,"%s/B0000106.3FR",album);fixture(p,"output fixture");CHECK(source_identity(p,outs[0]));
  snprintf(p,sizeof p,"%s/B0000099.3FR",album);fixture(p,"unrelated retain");
  int journal=open("journal",O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(journal>=0);
  if(scenario==1)hashes[2][0]='z';if(scenario==2)outs[0][0]='z';
  if(scenario==3){snprintf(p,sizeof p,"%s/B0000102.jpg",album);fixture(p,"unowned companion");}
  if(scenario==11)v.format=1;
  int imported=album_commit_verified(&v,"B0000106",outs,journal);
  CHECK(imported==(scenario!=2&&scenario!=9&&scenario!=10&&scenario!=11));
  int ok=imported&&cleanup_six("/mnt/d",&vol,album,six,hashes,&v,"B0000106",outs,journal);
  CHECK(ok==(scenario==0));
  int deleted=scenario==0?6:scenario==4?2:(scenario>=5&&scenario<=8)?1:0;
  CHECK(removes==(scenario==4?3:deleted));
  for(int i=0;i<6;i++){snprintf(p,sizeof p,"%s/%s",album,six[i].name);CHECK((access(p,F_OK)==0)==(i>=deleted));}
  snprintf(p,sizeof p,"%s/B0000106.3FR",album);CHECK(!access(p,F_OK));snprintf(p,sizeof p,"%s/B0000099.3FR",album);CHECK(!access(p,F_OK));
  close(journal);CHECK(!chdir(".."));
 }
 printf("ALBUM_JOB_CHECKS=%d SCENARIOS=12 MOCK_CLI_NOT_CAMERA\n",checks);return 0;
}
