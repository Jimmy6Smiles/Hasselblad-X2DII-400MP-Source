/* Fresh-job binding. Only the six symlinks created by the capture coordinator
 * are accepted; a template from another retained photograph is rejected. */
#ifndef NATIVE_JOB_INPUTS_H
#define NATIVE_JOB_INPUTS_H
#include "native_job_paths.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
/* Keep read-only descriptors open for the entire job. On the camera CFE,
 * an unreferenced inode can be evicted/recreated with another st_ino even
 * though bytes,size,mtime,ctime are unchanged. Pin it; do NOT weaken checks. */
typedef struct {char path[6][256];struct stat identity[6];int fd[6];} NativeJobInputs;
static void nji_close(NativeJobInputs*v){if(v)for(unsigned i=0;i<6;i++){if(v->fd[i]>=0)close(v->fd[i]);v->fd[i]=-1;}}
static int nji_raw_path(const char*s){
 const char*a="/mnt/media_rw/cfe/DCIM/",*b="/mnt/media_rw/ssd/DCIM/",*p=NULL;
 if(!s)return 0;
 if(!strncmp(s,a,strlen(a)))p=s+strlen(a);
 else if(!strncmp(s,b,strlen(b)))p=s+strlen(b);
 if(!p||strlen(p)!=21||p[0]<'1'||p[0]>'9')return 0;
 for(unsigned i=1;i<3;i++)if(p[i]<'0'||p[i]>'9')return 0;
 if(strncmp(p+3,"HASBL/B",7)||strcmp(p+17,".3FR"))return 0;
 for(unsigned i=10;i<17;i++)if(p[i]<'0'||p[i]>'9')return 0;
 return 1;
}
static int nji_same(const struct stat*a,const struct stat*b){
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int nji_open(NativeJobInputs*v,NativeJobPaths*p){
 if(!v||!p)return 0;memset(v,0,sizeof *v);
 for(unsigned i=0;i<6;i++)v->fd[i]=-1;
 for(unsigned i=0;i<6;i++){
  char link[192];snprintf(link,sizeof link,"%s/input%u.3fr",p->work,i);
  ssize_t n=readlink(link,v->path[i],sizeof v->path[i]-1);
  if(n<=0||(size_t)n>=sizeof v->path[i]-1)goto fail;v->path[i][n]=0;
  if(!nji_raw_path(v->path[i])||strncmp(v->path[i],p->stage,17))goto fail;
  if(i&&strncmp(v->path[0],v->path[i],30))goto fail; /* same DCIM album */
  v->fd[i]=open(v->path[i],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat named;
  if(v->fd[i]<0||fstat(v->fd[i],v->identity+i)||!S_ISREG(v->identity[i].st_mode)||
     v->identity[i].st_size<100000000||lstat(v->path[i],&named)||!nji_same(v->identity+i,&named))goto fail;
  for(unsigned k=0;k<i;k++)if(!strcmp(v->path[k],v->path[i])||
    (v->identity[k].st_dev==v->identity[i].st_dev&&v->identity[k].st_ino==v->identity[i].st_ino))goto fail;
 }
 strcpy(p->first,v->path[0]);return 1;
fail:nji_close(v);return 0;
}
static int nji_unchanged(const NativeJobInputs*v){
 struct stat s;for(unsigned i=0;i<6;i++){
  if(v->fd[i]<0||fstat(v->fd[i],&s)||!nji_same(v->identity+i,&s))return 0;
  int result=lstat(v->path[i],&s);
  if(result||!nji_same(v->identity+i,&s)){
#ifdef NATIVE_FACTORY_GRID
   if(result)printf("FACTORY_SOURCE_STAT_FAILED index=%u errno=%d path=%s\n",i,errno,v->path[i]);
   else{const struct stat*b=v->identity+i;
    printf("FACTORY_SOURCE_CHANGED index=%u dev=%llu/%llu ino=%llu/%llu size=%lld/%lld mtime=%lld.%09ld/%lld.%09ld ctime=%lld.%09ld/%lld.%09ld\n",i,(unsigned long long)b->st_dev,(unsigned long long)s.st_dev,(unsigned long long)b->st_ino,(unsigned long long)s.st_ino,(long long)b->st_size,(long long)s.st_size,(long long)b->st_mtim.tv_sec,b->st_mtim.tv_nsec,(long long)s.st_mtim.tv_sec,s.st_mtim.tv_nsec,(long long)b->st_ctim.tv_sec,b->st_ctim.tv_nsec,(long long)s.st_ctim.tv_sec,s.st_ctim.tv_nsec);
   }
#endif
   return 0;
  }
 }
 return 1;
}
static int nji_description(void*m,const char*source){
 Iter args={0},map={0};unsigned fields=0,hits=0;
 if(!dbus_message_iter_init(m,&args)||dbus_message_iter_get_arg_type(&args)!='a')return 0;
 dbus_message_iter_recurse(&args,&map);
 while(dbus_message_iter_get_arg_type(&map)){
  Iter pair={0},v={0};const char*k=NULL;
  if(++fields>64||dbus_message_iter_get_arg_type(&map)!='e')return 0;
  dbus_message_iter_recurse(&map,&pair);
  if(dbus_message_iter_get_arg_type(&pair)!='s')return 0;
  dbus_message_iter_get_basic(&pair,&k);
  if(!k||!dbus_message_iter_next(&pair)||dbus_message_iter_get_arg_type(&pair)!='v')return 0;
  dbus_message_iter_recurse(&pair,&v);
  if(!strcmp(k,"DESCRIPTION")){
   const char*s=NULL;if(hits++||dbus_message_iter_get_arg_type(&v)!='s')return 0;
   dbus_message_iter_get_basic(&v,&s);if(!s||strcmp(s,source))return 0;
  }
  if(!dbus_message_iter_next(&map))break;
 }
 return hits==1;
}
static int nji_args(NativeJobPaths*p,int argc,char**argv){
 if(argc!=5||strcmp(argv[1],"--job")||strlen(argv[4])!=16)return 0;
 uint64_t nonce=0;
 for(unsigned i=0;i<16;i++){
  unsigned char c=(unsigned char)argv[4][i];unsigned n;
  if(c>='0'&&c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;else return 0;
  nonce=(nonce<<4)|n;
 }
 return njp_init(p,argv[2],argv[3],nonce);
}
#endif
