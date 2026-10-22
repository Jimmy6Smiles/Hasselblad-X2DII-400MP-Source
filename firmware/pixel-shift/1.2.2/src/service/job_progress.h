/* 自写只读进度解析：仅读取本任务有界日志，不把 6/6 当作成片完成。 */
typedef struct {const char *phase;int capture_verified;} JobProgress;
static void progress_line(JobProgress *p,const char *line){
 long long tick;char stage[96],extra;
 if(sscanf(line,"%lld %95s %c",&tick,stage,&extra)!=2||tick<0)return;
 if(!strcmp(stage,"CAPTURE_SIX_VERIFIED")){p->capture_verified=1;p->phase="merge";}
 else if(!strcmp(stage,"MERGE"))p->phase="merge";
 else if(!strcmp(stage,"RAW_PACK")||!strcmp(stage,"JPEG_ENCODE"))p->phase="encode";
 else if(!strcmp(stage,"PUBLISH_INTENT_RETAIN_SIX"))p->phase="save";
 else if(!strcmp(stage,"FILES_PUBLISHED_BEFORE_INPUT_CLEANUP"))p->phase="album";
 else if(!strcmp(stage,"CLEANUP_STAGE_INTENT"))p->phase="cleanup";
 else if(!strcmp(stage,"ALBUM_IMPORT_UNIMPLEMENTED_RETAIN_SIX")||!strcmp(stage,"ALBUM_IMPORT_FAILED_RETAIN_SIX")||!strcmp(stage,"CLEANUP_PARTIAL_REQUIRES_REVIEW"))p->phase="recovery";
}
static JobProgress job_progress(uint64_t job){
 JobProgress p={job?"capture":"idle",0};if(!job)return p;
 char path[256],data[16385];snprintf(path,sizeof path,PS_DATA"/jobs/%llu/pipeline.log",(unsigned long long)job);
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return p;
 struct stat s;ssize_t n=-1;
 if(!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_uid==getuid()&&s.st_size<=16384)n=read(fd,data,sizeof data-1);
 close(fd);if(n<=0)return p;data[n]=0;
 char *start=data,*end;
 while((end=strchr(start,'\n'))){*end=0;progress_line(&p,start);start=end+1;}
 return p;
}
