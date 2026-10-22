/* Reboot recovery metadata only. Never opens or deletes photograph files. */
#ifndef PS_REBOOT_PENDING_H
#define PS_REBOOT_PENDING_H
#ifndef RP_DATA
#define RP_DATA "/data/x2d2-integrated-v1"
#endif
#ifndef RP_RAM
#define RP_RAM "/dev/x2d2-album-refresh-v1"
#endif
#ifndef RP_BOOT
#define RP_BOOT "/proc/sys/kernel/random/boot_id"
#endif
static int rp_read(const char *path,void *buf,size_t cap){
 struct stat s;int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 int ok=!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_uid==geteuid()&&s.st_nlink==1&&!(s.st_mode&0022)&&s.st_size>0&&(size_t)s.st_size<=cap;
 ssize_t n=ok?read(fd,buf,(size_t)s.st_size):-1;close(fd);
 return ok&&n==s.st_size?(int)n:-1;
}
static int rp_token(const char *s){
 size_t n=strlen(s);if(!n||n>20||s[0]=='0')return 0;
 for(size_t i=0;i<n;i++)if(s[i]<'0'||s[i]>'9')return 0;
 return 1;
}
static int rp_boot(char b[40]){
 memset(b,0,40);int fd=open(RP_BOOT,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 char x[40]={0};ssize_t n=read(fd,x,40);close(fd);if(n!=37||x[36]!='\n')return 0;
 for(int i=0;i<36;i++)if((i==8||i==13||i==18||i==23)?x[i]!='-':!((x[i]>='0'&&x[i]<='9')||(x[i]>='a'&&x[i]<='f')))return 0;
 memcpy(b,x,36);return 1;
}
static int rp_pending(char token[21]){
 memset(token,0,21);int n=rp_read(RP_DATA"/pending",token,20);return n>0&&rp_token(token);
}
static int rp_sync(const char *dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);close(fd);return ok;}
static int rp_new(const char *path,const char *data){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 size_t n=strlen(data);int ok=write(fd,data,n)==(ssize_t)n&&!fsync(fd);close(fd);return ok;
}
static int rp_archive(const char *src,const char *dst){
 /* No overwrite; a crash between link/unlink leaves evidence, not data loss. */
 if(link(src,dst))return 0;
 return unlink(src)==0;
}
static int rp_review(char token[21],int *resolved){
 char b[128]={0},boot[40],seen[40],id[21],extra;int status=-1;
 if(!rp_boot(boot)||rp_read(RP_RAM"/pending.reviewed",b,sizeof(b)-1)<0)return 0;
 if(sscanf(b,"%39s %20s %d %c",seen,id,&status,&extra)!=3||strcmp(boot,seen)||!rp_token(id)||(status!=0&&status!=1))return 0;
 strcpy(token,id);*resolved=status;return 1;
}
static int rp_menu_retry(const char *full){
 char token[21],path[256],job[192],once[240],archive[240],buf[128]={0};int resolved;
 struct stat st;
 if(!rp_review(token,&resolved))return 0;
 snprintf(path,sizeof path,"%s/accepted",full);
 if(rp_read(path,buf,sizeof buf-1)<0)return 0;
 snprintf(path,sizeof path,"%s/boot.pending",full);memset(buf,0,sizeof buf);
 if(rp_read(path,buf,sizeof buf-1)<0||strcmp(buf,"FULL_FIRST_BOOT_AWAITING_USER\n"))return 0;
 snprintf(job,sizeof job,RP_DATA"/jobs/%s",token);
 if(lstat(job,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 0;
 snprintf(once,sizeof once,"%s/menu-retry.used",job);snprintf(archive,sizeof archive,"%s/menu-boot.pending",job);
 return rp_new(once,"ONE_MENU_RETRY_AFTER_REBOOT_REVIEW\n")&&rp_sync(job)&&
  rp_archive(path,archive)&&rp_sync(job)&&rp_sync(full);
}
#endif
