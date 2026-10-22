/* 原厂删除操作按相册组处理：只接受本次 RAW 单文件，任何同名伴随文件均拒绝。 */
static int album_single_raw(const char *directory,const char *name){
 if(!entry_name(name)||strcmp(name+8,".3FR"))return 0;
 DIR *d=opendir(directory);if(!d)return 0;struct dirent *de;unsigned count=0;int ok=1;
 errno=0;
 while((de=readdir(d))){
  if(strlen(de->d_name)>=8&&!strncasecmp(name,de->d_name,8)&&(!de->d_name[8]||de->d_name[8]=='.')){
   if(strcmp(name,de->d_name)){ok=0;break;}count++;
  }
 }
 if(errno)ok=0;closedir(d);return ok&&count==1;
}
static int cleanup_six(const char *volume,const VolumeIdentity *vol,const char *source,
 const Entry six[6],char hashes[6][65],const Settings *v,
 const char *stem,char output_hashes[2][65],int journal){
 char folder[14],display[32],album[256],path[512],output[512],digest[65];struct stat st;
 if(journal<0||stopping||v->format!=0||!volume_same(volume,vol)||
    !album_names(v,stem,folder,display,album)||strcmp(source,album))return 0;
 snprintf(output,sizeof output,"%s/%s.3FR",album,stem);
 if(!regular(output,&st)||!source_hash(output,digest)||strcmp(digest,output_hashes[0])||!album_metadata(display,output,st.st_size))return 0;
 /* 六张全部完成事前核验才允许删第一张；日志保留全部摘要及操作意图。 */
 for(int i=0;i<6;i++){
  if(!entry_name(six[i].name)||strcmp(six[i].name+8,".3FR")||!strncmp(six[i].name,stem,8))return 0;
  for(int j=0;j<i;j++)if(!strcmp(six[i].name,six[j].name))return 0;
  if(!album_single_raw(source,six[i].name)||!join(path,sizeof path,source,six[i].name)||!regular(path,&st)||
     st.st_dev!=vol->dev||st.st_size!=six[i].size||!source_hash(path,digest)||strcmp(digest,hashes[i]))return 0;
  snprintf(display,sizeof display,"%s/%.8s",folder,six[i].name);
  if(!album_metadata(display,path,st.st_size))return 0;
 }
 if(!checkpoint(journal,"CLEANUP_STAGE_INTENT"))return 0;
 for(int i=0;i<6;i++){
  if(stopping||!volume_same(volume,vol)||!album_single_raw(source,six[i].name)||
     !join(path,sizeof path,source,six[i].name)||!regular(path,&st)||st.st_size!=six[i].size||
     st.st_dev!=vol->dev||!source_hash(path,digest)||strcmp(digest,hashes[i]))goto uncertain;
  snprintf(display,sizeof display,"%s/%.8s",folder,six[i].name);
  if(!album_metadata(display,path,st.st_size)||dprintf(journal,"NATIVE_REMOVE_INTENT %s %s\n",display,hashes[i])<0||fsync(journal))goto uncertain;
  char reply[8192];char *av[]={PS_ODIN,"-s","storage","-m","remove",display,NULL};
  /* 每张恰好调用一次；超时/错误/部分删除均保留剩余原片，绝不重发。 */
  if(!PS_ALBUM_COMMAND(av,reply,sizeof reply,26000,-1))goto uncertain;
  if(!lstat(path,&st)||errno!=ENOENT||!volume_same(volume,vol)||!album_absent(display)||!sync_dir(source))goto uncertain;
  if(dprintf(journal,"NATIVE_REMOVED_FILE_AND_INDEX %s\n",display)<0||fsync(journal))goto uncertain;
 }
 if(!source_hash(output,digest)||strcmp(digest,output_hashes[0]))goto uncertain;
 snprintf(display,sizeof display,"%s/%s",folder,stem);
 if(!regular(output,&st)||!album_metadata(display,output,st.st_size))goto uncertain;
 return checkpoint(journal,"SIX_INPUTS_DELETED");
uncertain:
 checkpoint(journal,"CLEANUP_PARTIAL_REQUIRES_REVIEW");return 0;
}
