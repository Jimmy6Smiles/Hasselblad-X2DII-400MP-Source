#include <spawn.h>
#include <sys/wait.h>
#include "hardware_gdc_runtime_plan.h"
#include "native_stream_cache.h"
extern char **environ;
static NativeStreamCache stream_cache;
static int stream_fd=-1;
static pid_t stream_pid=-1;
static int stream_write(const void*p,size_t n){while(n){if(cancelled)return 0;ssize_t z=write(stream_fd,p,n);if(z<0&&errno==EINTR)continue;if(z<=0)return 0;p=(const char*)p+z;n-=z;}return 1;}
static int stream_consume(void*unused,unsigned index,const unsigned char*p,size_t n){
 (void)unused;uint32_t header[2]={index,(uint32_t)n};return n<=UINT32_MAX&&stream_write(header,sizeof header)&&stream_write(p,n);
}
static int stream_finish(void){
 if(stream_fd>=0){close(stream_fd);stream_fd=-1;}int status=0;
 if(stream_pid<=0)return 0;
 pid_t result;do{result=waitpid(stream_pid,&status,0);}while(result<0&&errno==EINTR);
 stream_pid=-1;return result>0&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int stream_begin(const char*stage,uint64_t nonce){
 char grid[256],hex[17];snprintf(hex,sizeof hex,"%016llx",(unsigned long long)nonce);
 snprintf(grid,sizeof grid,"/dev/x2d2-pregdc-trial/grid-%s.bin",hex);
 int fd=open(grid,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;unsigned char*b=NULL;NativeFactoryMesh m={0};int ok=0;
 if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<NFGB_HEADER||st.st_size>NFGB_LIMIT)goto done;
 b=malloc(st.st_size);if(!b)goto done;size_t at=0;
 while(at<(size_t)st.st_size){ssize_t n=read(fd,b+at,st.st_size-at);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;at+=n;}
 if(!nfgb_decode(&m,nonce,b,at)||!hgp_make(&m,nonce)||!nsc_init(&stream_cache,160u*1024*1024))goto done;
 /* Predict exact allocation high-water for this mesh and fixed owner order. */
 uint64_t peak=nsc_required(&stream_cache);
 unsigned long available=0,total=0;char line[256];FILE*f=fopen("/proc/meminfo","r");
 if(f){while(fgets(line,sizeof line,f)){sscanf(line,"MemAvailable: %lu kB",&available);sscanf(line,"MemTotal: %lu kB",&total);}fclose(f);}
 printf("STREAM_CACHE_BUDGET peak=%llu available_kb=%lu total_kb=%lu\n",(unsigned long long)peak,available,total);
 if(total&&(uint64_t)available*1024<peak+(uint64_t)total*1024/5+32u*1024*1024){
  /* Spill only cross-window boundary pieces; never a full rendered raster.
   * Unlink immediately: close/crash releases the job-private scratch file. */
  char scratch[320];int n=snprintf(scratch,sizeof scratch,"%s/stream-boundary-XXXXXX",stage);
  if(n<0||(size_t)n>=sizeof scratch)goto done;
  int cachefd=mkstemp(scratch);if(cachefd<0)goto done;
  if(unlink(scratch)){close(cachefd);goto done;}
  fcntl(cachefd,F_SETFD,FD_CLOEXEC);stream_cache.spill=fdopen(cachefd,"w+b");
  if(!stream_cache.spill){close(cachefd);goto done;}
  peak=nsc_required(&stream_cache);
  printf("STREAM_BOUNDARY_SPILL_BUDGET peak=%llu\n",(unsigned long long)peak);
 }
 if(!total||peak>stream_cache.limit||(uint64_t)available*1024<peak+(uint64_t)total*1024/5+32u*1024*1024)goto done;
 int pipes[2];if(pipe2(pipes,O_CLOEXEC))goto done;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions)){close(pipes[0]);close(pipes[1]);goto done;}
 int action=posix_spawn_file_actions_adddup2(&actions,pipes[0],0)|posix_spawn_file_actions_addclose(&actions,pipes[1]);
 char worker[]="/dev/x2d2-integrated-v1/jpeg-stream-worker";
 char*args[]={worker,"pipe",(char*)stage,grid,hex,NULL};
 int spawned=action?-1:posix_spawn(&stream_pid,args[0],&actions,NULL,args,environ);
 posix_spawn_file_actions_destroy(&actions);close(pipes[0]);
 if(spawned){close(pipes[1]);stream_pid=-1;goto done;}stream_fd=pipes[1];signal(SIGPIPE,SIG_IGN);ok=1;
done:if(fd>=0)close(fd);free(b);nfm_free(&m);return ok;
}
