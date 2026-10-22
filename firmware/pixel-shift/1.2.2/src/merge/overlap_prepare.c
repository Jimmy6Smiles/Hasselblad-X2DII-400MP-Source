/* 私有离线实验：调用方明确声明某帧已保存后才处理；不监控相机、不推测完成。
 * 协议 READY 0..5 按顺序逐帧确认，最后 FINISH；CANCEL/信号/EOF 不发布任务。
 * 不带照片删除、拍摄、原厂接口或自动回退执行。 */
#define _GNU_SOURCE
#include <signal.h>
static volatile sig_atomic_t stop;
#define PS_PREPARE_CANCELLED() (stop)
#define PS_PREPARE_LIBRARY
#include "auto_prepare.c"
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
#include <errno.h>
static void stopped(int s){(void)s;stop=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int same(const struct stat *a,const struct stat *b){
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
#include "overlap_prefix.h"
static OpPixel *prefix;
/* Exact copies of only the rows consumed by the prefix, about 18 MiB total.
 * Rebinding a replaced file requires fresh calibration AND byte equality. */
static unsigned char *consumed[6];
static int held[6]={-1,-1,-1,-1,-1,-1};
static int same_rows(FILE *f,const Input *in,unsigned index){
 unsigned char row[23808];unsigned end=index<4?OP_ROWS+1:OP_ROWS;
 static const unsigned dy[]={0,1,0,1};
 if(!consumed[index])return 0;
 for(unsigned y=0;y<end;y++){
  unsigned sy=index<4?y+1-dy[index]:y+(index==4);
  if(stop||!fm_read(f,in->size,in->offset+(uint64_t)(96+sy)*23808,row,sizeof row)||
     memcmp(row,consumed[index]+(size_t)y*sizeof row,sizeof row))return 0;
 }
 return 1;
}
static int accumulate(FILE *f,const Input *in,unsigned index){
 if(!prefix){prefix=calloc((size_t)OP_ROWS*OP_WIDTH,sizeof *prefix);if(!prefix)return 0;}
 uint16_t *lut=malloc(4*65536*sizeof *lut);unsigned char row[23808];if(!lut)return 0;
 for(unsigned c=0;c<4;c++)for(unsigned v=0;v<65536;v++)lut[c*65536+v]=op_normalize(v,in->black[c],in->white);
 int ok=1;unsigned end=index<4?OP_ROWS+1:OP_ROWS;
 consumed[index]=malloc((size_t)end*sizeof row);
 if(!consumed[index]){free(lut);return 0;}
 static const unsigned dx[]={0,0,1,1},dy[]={0,1,0,1};
 for(unsigned y=0;y<end;y++){
  unsigned sy=index<4?y+1-dy[index]:y+(index==4);
  if(stop||!fm_read(f,in->size,in->offset+(uint64_t)(96+sy)*23808,row,sizeof row)){ok=0;break;}
  memcpy(consumed[index]+(size_t)y*sizeof row,row,sizeof row);
  for(unsigned x=0;x<OP_WIDTH;x++){
   unsigned sx=x+(index<4?dx[index]:1),c=op_channel(sx,sy);
   unsigned v=lut[c*65536+fm16(row+2*(128+sx))];
   if(index<4)op_add_integer(prefix,y,x,c,v);else op_add_half(prefix,y,x,c,v);
  }
 }
 free(lut);return ok;
}
static int emit_prefix(int dir,const struct stat ids[6]){
 OpMeta meta={.magic="PSPFX01",.width=OP_WIDTH,.rows=OP_ROWS};
 for(unsigned i=0;i<6;i++)op_identity(meta.identities[i],ids+i);
 int j=openat(dir,"job.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(j<0)return 0;
 ssize_t n=read(j,meta.job,256);close(j);if(n!=256)return 0;
 int fd=openat(dir,"prefix.raw",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 FILE *f=fdopen(fd,"wb");if(!f){close(fd);return 0;}
 uint16_t *row=malloc(OP_WIDTH*8);int ok=row!=NULL;ps_digest_init(&meta.digest);
 for(unsigned y=0;ok&&y<OP_ROWS;y++){
  if(stop){ok=0;break;}op_render(prefix,y,row);
  if(fwrite(row,8,OP_WIDTH,f)!=OP_WIDTH){ok=0;break;}
  ps_digest_update(&meta.digest,row,OP_WIDTH*8);
 }
 free(row);if(fflush(f)||fsync(fd))ok=0;if(fclose(f))ok=0;
 if(ok)ok=write_new(dir,"prefix.meta",&meta,sizeof meta)&&!fsync(dir);
 return ok;
}
int main(int argc,char **argv){
 if(argc!=3||strcmp(argv[1],"--stream")||argv[2][0]!='/')return 2;
 struct sigaction sa={0};sa.sa_handler=stopped;sigemptyset(&sa.sa_mask);
 if(sigaction(SIGTERM,&sa,NULL)||sigaction(SIGINT,&sa,NULL))return 2;
 /* 单线程、低 CPU 优先级；真实设备上的 I/O 竞争仍须另测。 */
 if(setpriority(PRIO_PROCESS,0,10))return 2;
 int dir=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 2;
 Input inputs[6]={0};FmIfd roots[6]={0};struct stat ids[6],st;unsigned count=0;int rc=1;
 char line[32];double total=0;int refreshed=0;
 if(fstatat(dir,"job.bin",&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)goto done;
 if(fstatat(dir,"header.dng",&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)goto done;
 while(!stop&&fgets(line,sizeof line,stdin)){
  if(!strcmp(line,"CANCEL\n")){stop=1;break;}
  if(!strcmp(line,"FINISH\n")||!strcmp(line,"FINISH_REFRESH\n")){
   int refresh=!strcmp(line,"FINISH_REFRESH\n");
   if(count!=6)goto done;
   for(unsigned i=0;i<6;i++){
    char name[32];snprintf(name,sizeof name,"input%u.3fr",i);
    if(fstatat(dir,name,&st,0))goto done;
    if(!same(&ids[i],&st)){
     printf("PREFIX_ID_CHANGED index=%u ino=%llu/%llu mt=%lld.%09ld/%lld.%09ld ct=%lld.%09ld/%lld.%09ld\n",i,
      (unsigned long long)ids[i].st_ino,(unsigned long long)st.st_ino,
      (long long)ids[i].st_mtim.tv_sec,ids[i].st_mtim.tv_nsec,(long long)st.st_mtim.tv_sec,st.st_mtim.tv_nsec,
      (long long)ids[i].st_ctim.tv_sec,ids[i].st_ctim.tv_nsec,(long long)st.st_ctim.tv_sec,st.st_ctim.tv_nsec);
     /* Never accept a changed identity as the same cached input. Throw away
      * ALL parsed metadata/calibration for that frame and read it anew. */
     if(!refresh)goto done;
     Input previous=inputs[i];
     int fd=openat(dir,name,O_RDONLY|O_CLOEXEC);if(fd<0)goto done;
     int pin=fcntl(fd,F_DUPFD_CLOEXEC,3);if(pin<0){close(fd);goto done;}
     if(held[i]>=0)close(held[i]);held[i]=pin;
     FILE *f=fdopen(fd,"rb");if(!f){close(fd);goto done;}
     fm_free(&roots[i]);memset(&roots[i],0,sizeof roots[i]);memset(&inputs[i],0,sizeof inputs[i]);
     double begin=now();
     int ok=!fstat(fd,&ids[i])&&S_ISREG(ids[i].st_mode)&&parse(f,&inputs[i],&roots[i]);
     int equal=ok&&!memcmp(previous.black,inputs[i].black,sizeof previous.black)&&
       previous.white==inputs[i].white&&
       exposure_equal(&previous.exposure,&inputs[i].exposure)&&
       same_rows(f,&inputs[i],i);
     ok=ok&&!fstat(fd,&st)&&same(&ids[i],&st);
     fclose(f);if(!ok||stop)goto done;
     if(!equal)refreshed=1;
     printf("PREFIX_REFRESH_CHECK index=%u exact_equal=%d\n",i,equal);
     double elapsed=now()-begin;total+=elapsed;
     printf("FRAME_REFRESHED %u seconds=%.6f\n",i,elapsed);fflush(stdout);
     if(i){fm_free(&roots[i]);memset(&roots[i],0,sizeof roots[i]);}
    }
   }
   for(unsigned i=0;i<6;i++){
    char name[32];snprintf(name,sizeof name,"input%u.3fr",i);
    if(fstatat(dir,name,&st,0)||!same(&ids[i],&st))goto done;
    for(unsigned j=0;j<i;j++)if((ids[i].st_dev==ids[j].st_dev&&ids[i].st_ino==ids[j].st_ino)||
      !exposure_equal(&inputs[i].exposure,&inputs[j].exposure))goto done;
   }
   /* 写小型任务文件期间屏蔽终止，检查待处理信号；发布前取消则不写。
    * 发布后仍由协调器负责取消，不能把本工具返回当成完整拍摄成功。 */
   sigset_t mask,old,pending;sigemptyset(&mask);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGINT);
   if(sigprocmask(SIG_BLOCK,&mask,&old))goto done;
   if(sigpending(&pending)||stop||sigismember(&pending,SIGTERM)||sigismember(&pending,SIGINT))stop=1;
   else {
    int domain_ok=1;
    for(unsigned i=1;i<6;i++)if(memcmp(inputs[i].black,inputs[0].black,sizeof inputs[0].black))domain_ok=0;
    if(domain_ok&&emit_six(dir,inputs,&roots[0]))rc=0;
   }
   sigprocmask(SIG_SETMASK,&old,NULL);
   if(!rc&&!stop&&!refreshed){
    double begin=now();if(!emit_prefix(dir,ids)){rc=1;goto done;}
    printf("PIXEL_PREFIX_FINALIZED rows=%u seconds=%.6f at=%.6f\n",OP_ROWS,now()-begin,now());
   }else if(refreshed)puts("PIXEL_PREFIX_DISCARDED_IDENTITY_CHANGED");
   if(!rc&&!stop)printf("STREAM_PREPARED frames=6 preparation_seconds=%.6f\n",total);
   break;
  }
  char expected[32];snprintf(expected,sizeof expected,"READY %u\n",count);
  if(count>=6||strcmp(line,expected))goto done;
  char name[32];snprintf(name,sizeof name,"input%u.3fr",count);
  int fd=openat(dir,name,O_RDONLY|O_CLOEXEC);if(fd<0)goto done;
  held[count]=fcntl(fd,F_DUPFD_CLOEXEC,3);if(held[count]<0){close(fd);goto done;}
  FILE *f=fdopen(fd,"rb");if(!f){close(fd);goto done;}
  double begin=now();
  int ok=!fstat(fd,&ids[count])&&parse(f,&inputs[count],&roots[count])&&
    accumulate(f,&inputs[count],count)&&
    !fstat(fd,&st)&&same(&ids[count],&st);
  fclose(f);if(!ok||stop)goto done;
  for(unsigned i=0;i<count;i++)if((ids[i].st_dev==ids[count].st_dev&&ids[i].st_ino==ids[count].st_ino)||
    !exposure_equal(&inputs[i].exposure,&inputs[count].exposure))goto done;
  double elapsed=now()-begin;total+=elapsed;
  printf("PIXEL_FRAME_ACCUMULATED index=%u rows=%u begin=%.6f end=%.6f\n",count,OP_ROWS,begin,now());
  printf("FRAME_PREPARED %u seconds=%.6f\n",count,elapsed);fflush(stdout);count++;
  /* Only first-frame metadata is used by emit_six; later IFD trees no longer
   * need to remain resident. Input calibration/exposure stays in small structs. */
  if(count>1){fm_free(&roots[count-1]);memset(&roots[count-1],0,sizeof roots[count-1]);}
 }
done:
 for(unsigned i=0;i<6;i++)free(consumed[i]);
 for(unsigned i=0;i<6;i++)if(held[i]>=0)close(held[i]);
 free(prefix);prefix=NULL;
 for(unsigned i=0;i<6;i++)fm_free(&roots[i]);
 close(dir);if(stop)rc=130;
 if(rc)fprintf(stderr,"STREAM_NOT_PUBLISHED_OR_INCOMPLETE rc=%d frames=%u\n",rc,count);
 return rc;
}
