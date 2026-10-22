#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <dirent.h>
#include <sys/syscall.h>
#include "native_replay_cache.h"
static int directory(const char*p){struct stat s;return !lstat(p,&s)&&S_ISDIR(s.st_mode);}
static int syncdir(const char*p){int fd=open(p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);close(fd);return ok;}
static int prune(const char*volume){
 if(strcmp(volume,"cfe")&&strcmp(volume,"ssd"))return 0;
 char root[512];snprintf(root,sizeof root,NRC_MEDIA"/%s/.x2d2-pixelshift/replay",volume);
 DIR*d=opendir(root);if(!d)return errno==ENOENT;struct dirent*e;int ok=1;
 while((e=readdir(d))){
  if(strlen(e->d_name)!=22||e->d_name[8]!='-'||strcmp(e->d_name+17,".bind"))continue;
  char item[32],raw[512],index[512],jpeg[512];NrcBinding b;NrcIdentity r,j;
  snprintf(item,sizeof item,"/%s/%.8s/%.8s",volume,e->d_name,e->d_name+9);
  if(!nrc_paths(item,raw,index,sizeof raw)||!nrc_read(index,&b)||!nrc_jpeg_path(item,&b,jpeg,sizeof jpeg))continue;
  struct stat st;int absent=lstat(raw,&st)&&errno==ENOENT;
  if(!absent&&(!nrc_identity(raw,&r)||!memcmp(&r,&b.raw,sizeof r)))continue;
  if(nrc_identity(jpeg,&j)&&!memcmp(&j,&b.jpeg,sizeof j)){
   if(unlink(jpeg)){ok=0;continue;}
  }else if(!lstat(jpeg,&st)||errno!=ENOENT){ok=0;continue;}
  if(unlink(index))ok=0;
 }
 closedir(d);return syncdir(root)&&ok;
}
int main(int argc,char**argv){
 if(argc==3&&!strcmp(argv[1],"--check")){char jpeg[512];int ok=nrc_lookup(argv[2],jpeg,sizeof jpeg);if(ok==1)puts("NATIVE_REPLAY_BINDING_VALID");return ok==1?0:1;}
 if(argc==3&&!strcmp(argv[1],"--prune"))return prune(argv[2])?0:1;
 if(argc!=4||strcmp(argv[1],"--publish")||!nrc_item(argv[2])||!nrc_job(argv[3]))return 2;
 char raw[512],index[512],root[512],jpeg[512];NrcBinding b={0};
 memcpy(b.magic,"X2D2REPLAYv1",12);strcpy(b.job,argv[3]);
 if(!nrc_paths(argv[2],raw,index,sizeof raw)||!nrc_jpeg_path(argv[2],&b,jpeg,sizeof jpeg))return 2;
 strcpy(root,index);char*slash=strrchr(root,'/');if(!slash)return 2;*slash=0;
 if(mkdir(root,0700)&&errno!=EEXIST)return 3;if(!directory(root))return 3;
 if(!nrc_identity(raw,&b.raw)||!nrc_identity(jpeg,&b.jpeg)||b.raw.size<815010840||b.jpeg.size<10000)return 4;
 char part[544];snprintf(part,sizeof part,"%s.%s.partial",index,b.job);
 int fd=open(part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 5;
 int ok=write(fd,&b,sizeof b)==sizeof b&&!fsync(fd);if(close(fd))ok=0;
 if(!ok){unlink(part);return 6;}
 /* linkat does not work on exFAT. renameat2 NOREPLACE is used elsewhere by
  * the camera coordinator; keep the same no-clobber publication boundary. */
 if(syscall(SYS_renameat2,AT_FDCWD,part,AT_FDCWD,index,1)){unlink(part);return 7;}
 if(!syncdir(root)||nrc_lookup(argv[2],jpeg,sizeof jpeg)!=1)return 8;
 /* Only this validated job's known encoder tiles; no recursive cleanup. */
 char stage[512];strcpy(stage,jpeg);*strrchr(stage,'/')=0;
 int d=open(stage,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(d>=0){for(unsigned i=0;i<210;i++){char n[48];snprintf(n,sizeof n,"render-tile-%u.jpg",i);struct stat s;if(!fstatat(d,n,&s,AT_SYMLINK_NOFOLLOW)&&S_ISREG(s.st_mode))unlinkat(d,n,0);}fsync(d);close(d);}
 puts("NATIVE_REPLAY_CACHE_PUBLISHED");return 0;
}
