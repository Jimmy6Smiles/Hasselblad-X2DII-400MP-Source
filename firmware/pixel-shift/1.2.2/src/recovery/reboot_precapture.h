/* Narrow legacy pre-write rejection proof. Do not infer safety from missing
 * journals alone. No image files or exposure setters are used here. */
#include <dirent.h>
static int precap_files(const char *job){
 DIR *d=opendir(job);if(!d)return 0;struct dirent *e;int ok=1;
 while((e=readdir(d))){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  if(strcmp(e->d_name,"before.bin")&&strcmp(e->d_name,"pipeline.log")&&
     strcmp(e->d_name,"children.log")&&strcmp(e->d_name,"ae.journal")&&
     strcmp(e->d_name,"precap-mode.archived")){ok=0;break;}
  char path[512];struct stat st;
  int n=snprintf(path,sizeof path,"%s/%s",job,e->d_name);
  if(n<0||(size_t)n>=sizeof path){ok=0;break;}
  if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&0022)){ok=0;break;}
 }
 closedir(d);return ok;
}
static int precap_proof(const char *job,const char boot[40],int *archived){
 char p[256],log[8193]={0};CaptureRecord mode;struct stat st;
 if(!precap_files(job))return 0;
 snprintf(p,sizeof p,"%s/ae.journal",job);
 if(lstat(p,&st)||!S_ISREG(st.st_mode)||st.st_size!=0)return 0;
 snprintf(p,sizeof p,"%s/pipeline.log",job);
 int n=rp_read(p,log,sizeof(log)-1);
 const char *end=" PREFLIGHT_REJECTED_NO_SETTINGS_OR_CAPTURE\n";
 if(n<0||(size_t)n<strlen(end)||strcmp(log+n-strlen(end),end))return 0;
 snprintf(p,sizeof p,"%s/children.log",job);memset(log,0,sizeof log);
 if(rp_read(p,log,sizeof(log)-1)<0||!strstr(log,"\nAE_SNAPSHOT_FAILED idle_before\nAE_PREFLIGHT_NO_WRITES\n")||
    !strstr(log,"STREAM_SHADOW_END fault=0 cancelled=0 candidates=0 prepared=0 finished=0\n")||strstr(log,"AUTO6_REQUEST_ONCE"))return 0;
 *archived=0;
 if(lstat(RP_DATA"/mode-prepare.journal",&st)==0){
  snprintf(p,sizeof p,"%s/precap-mode.archived",job);
  if(lstat(p,&st)==0||errno!=ENOENT)return 0;
  snprintf(p,sizeof p,RP_DATA"/mode-prepare.journal");
 }else{
  if(errno!=ENOENT)return 0;
  *archived=1;snprintf(p,sizeof p,"%s/precap-mode.archived",job);
 }
 if(rp_read(p,&mode,sizeof mode)!=sizeof mode||memcmp(mode.magic,"X2DCAP2",8)||
    mode.check!=capture_hash(&mode)||mode.original!=0||mode.drive!=0||mode.phase!=1||!old_boot(mode.boot,boot))return 0;
 int32_t multi_now,drive_now;
 return idle()&&factory_error_clear()&&get_multi(&multi_now)&&multi_now==0&&
  get_drive(&drive_now)&&drive_now==0&&idle()&&factory_error_clear();
}
