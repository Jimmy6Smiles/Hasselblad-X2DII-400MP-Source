/* Fixed retained-job experiment. Uses job-specific compiled meshes, exclusive
 * output and bounded reusable ION. Not a generic deployed capture backend. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "native_jpeg_layout.h"
#include "nv16_window_read.h"
#ifdef GDCF_RUNTIME
#include "hardware_gdc_runtime_plan.h"
#else
#error Build this source with GDCF_RUNTIME; private calibration fixtures are not distributed.
#endif
extern int duss_hal_initialize(void*),duss_hal_deinitialize(void);
extern int duss_hal_attach_ion_mem(const char*,void**),duss_hal_detach_ion_mem(void*);
extern int duss_hal_device_open(const char*,void*,void**),duss_hal_device_start(void*,void*);
extern int duss_hal_device_stop(void*,void*),duss_hal_device_close(void*);
extern int duss_hal_mem_alloc(void*,void**,uint32_t,uint32_t,uint32_t,uint32_t);
extern int duss_hal_mem_map(void*,void**),duss_hal_mem_unmap(void*),duss_hal_mem_sync(void*,int),duss_hal_mem_free(void*);
extern int gdc2_hal_open(void**),gdc2_hal_set_param(void*,void*),gdc2_hal_process_async(void*,void*),gdc2_hal_get_buf(void*,void*);
extern void gdc2_hal_close(void*);
static volatile sig_atomic_t cancelled;
static void cancel(int n){(void)n;cancelled=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
extern int duss_hal_attach_verislcon_ienc(const char*,void**),duss_hal_detach_verislcon_ienc(void*);
static char tile_path[512];
static int native_verify_jpeg(const unsigned char*p,unsigned n,unsigned w,unsigned h){
 (void)w;(void)h;int fd=open(tile_path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 unsigned at=0;int ok=1;while(at<n){ssize_t z=write(fd,p+at,n-at);if(z<0&&errno==EINTR)continue;if(z<=0){ok=0;break;}at+=z;}
 if(fsync(fd))ok=0;if(close(fd))ok=0;if(!ok)unlink(tile_path);return ok;
}
#define NATIVE_JPEG_VERIFY
#define NATIVE_JPEG_QUALITY 90
#define NATIVE_JPEG_CAPACITY (16u*1024u*1024u)
#include "native_jpeg_trial.h"
static int pipe_read(void*p,size_t n){while(n){if(cancelled)return 0;ssize_t z=read(0,p,n);if(z<0&&errno==EINTR)continue;if(z<=0)return 0;p=(char*)p+z;n-=z;}return 1;}
static int transfer(int fd,void*p,size_t n,uint64_t off,int writing){
 while(n){if(cancelled)return 0;ssize_t k=writing?pwrite(fd,p,n,off):pread(fd,p,n,off);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;p=(char*)p+k;n-=k;off+=k;}return 1;
}
static int crop_read(void*ctx,unsigned char*p,size_t n,uint64_t off){return transfer(*(int*)ctx,p,n,off,0);}
static int same(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
int main(int argc,char**argv){
#ifdef GDCF_RUNTIME
 if(argc!=5||strlen(argv[4])!=16)return 2;
 uint64_t nonce=0;for(unsigned k=0;k<16;k++){char c=argv[4][k];unsigned v;if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else return 2;nonce=(nonce<<4)|v;}if(!nonce)return 2;
 int gridfd=-1;unsigned char*bundle=NULL;NativeFactoryMesh mesh={0};struct stat gridstat;
#else
 if(argc!=3)return 2;
#endif
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,cancel);signal(SIGINT,cancel);
 int source=-1,file=-1,owned=0,init=0,started=0,rc=1,pending=0;
 void*ion=NULL,*ctx=NULL,*input=NULL,*output=NULL,*grid=NULL,*map=NULL,*mapped_owner=NULL;uint32_t config=0;
 unsigned char*band=NULL;
 struct stat after;double begin=now(),input_time=0,hardware_time=0,output_time=0;
#ifdef GDCF_RUNTIME
 gridfd=open(argv[3],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(gridfd<0||fstat(gridfd,&gridstat)||!S_ISREG(gridstat.st_mode)||gridstat.st_nlink!=1||gridstat.st_size<NFGB_HEADER||gridstat.st_size>NFGB_LIMIT)goto end;
 bundle=malloc(gridstat.st_size);
 if(!bundle||!transfer(gridfd,bundle,gridstat.st_size,0,0)||!nfgb_decode(&mesh,nonce,bundle,gridstat.st_size)||!hgp_make(&mesh,nonce))goto end;
 free(bundle);bundle=NULL;nfm_free(&mesh);
#endif
 unsigned seen[TILE_COUNT]={0};
 unsigned long ram=0;unsigned long long free_bytes=0;char line[256];FILE*f=fopen("/proc/meminfo","r");
 if(f){while(fgets(line,sizeof line,f))if(sscanf(line,"MemAvailable: %lu kB",&ram)==1)break;fclose(f);}
 f=fopen("/sys/kernel/debug/ion/heaps/carveout_heap7","r");if(f){while(fgets(line,sizeof line,f))sscanf(line," free size: %llu",&free_bytes);fclose(f);}
 if(ram<128*1024||free_bytes<(uint64_t)GDCF_INPUT+GDCF_OUTPUT+GDCF_GRID+NATIVE_JPEG_CAPACITY+512u*1024*1024)goto end;
#if GDCF_BAND_ROWS
 /* No full-width output band allocation. */
#endif
 struct Module{const char*name;int(*attach)(const char*,void**);int(*detach)(void*);void*handle;};
 struct Module modules[]={{"/dev/ion",duss_hal_attach_ion_mem,duss_hal_detach_ion_mem,NULL},{"/dev/ienc0",duss_hal_attach_verislcon_ienc,duss_hal_detach_verislcon_ienc,NULL},{0}};
 if(duss_hal_initialize(modules))goto end;init=1;
 if(duss_hal_device_open("/dev/ion",&config,&ion)||duss_hal_device_start(ion,&config))goto end;started=1;
 if(duss_hal_mem_alloc(ion,&input,GDCF_INPUT,4096,2,0)||!input||duss_hal_mem_alloc(ion,&output,GDCF_OUTPUT,4096,2,0)||!output||duss_hal_mem_alloc(ion,&grid,GDCF_GRID,4096,2,0)||!grid)goto end;
 if(gdc2_hal_open(&ctx)||!ctx)goto end;
 for(unsigned count=0;count<TILE_COUNT;count++){
  uint32_t message[2];if(!pipe_read(message,sizeof message)||message[0]>=TILE_COUNT||seen[message[0]])goto end;
  unsigned index=message[0];seen[index]=1;
  const GdcTile*t=&tiles[index];uint32_t ib=t->stride*t->rows*2,ob=t->out_stride*t->ph*2;
  if(cancelled||ib>GDCF_INPUT||ob>GDCF_OUTPUT||t->bytes>GDCF_GRID)goto end;
  double start=now();
  if(duss_hal_mem_map(input,&map)||!map)goto end;mapped_owner=input;
  if(message[1]!=ib||!pipe_read(map,ib))goto end;
  if(duss_hal_mem_sync(input,2)||duss_hal_mem_unmap(input))goto end;map=NULL;mapped_owner=NULL;
  if(duss_hal_mem_map(grid,&map)||!map)goto end;mapped_owner=grid;memcpy(map,t->grid,t->bytes);
  if(duss_hal_mem_sync(grid,2)||duss_hal_mem_unmap(grid))goto end;map=NULL;mapped_owner=NULL;
  input_time+=now()-start;
  unsigned char options[0x28]={0},split[0x108]={0},proc[0xc8]={0},in[NJ_FRAME_BYTES],out[NJ_FRAME_BYTES],stream[NJ_STREAM_BYTES];
  if(!nj_layout(in,stream,(uintptr_t)input,(uintptr_t)output,t->stride,t->rows,t->stride,t->stride*t->rows,ib,ob)||
     !nj_layout(out,stream,(uintptr_t)output,(uintptr_t)input,t->pw,t->ph,t->out_stride,t->out_stride*t->ph,ob,ib))goto end;
  nj32(split,0,1);nj32(split,4,1);nj32(split,16,t->stride);nj32(split,20,t->rows);nj32(split,32,t->pw);nj32(split,36,t->ph);
  nj32(options,0,103);njptr(options,0x10,(uintptr_t)split);
  if(gdc2_hal_set_param(ctx,options))goto end;
  proc[0]=1;njptr(proc,8,(uintptr_t)grid);njptr(proc,0x88,(uintptr_t)in);njptr(proc,0x90,(uintptr_t)out);njptr(proc,0x98,(uintptr_t)proc);
  start=now();if(gdc2_hal_process_async(ctx,proc))goto end;pending=1;
  for(unsigned tries=0;;tries++){
   void*returned[3]={0};int native=gdc2_hal_get_buf(ctx,returned);
   if(!native&&returned[0]==in&&returned[1]==out&&returned[2]==proc){pending=0;break;}
   printf("GDC_WAIT TILE %u TRY %u RC %d RETAINING_DMA_OWNERS\n",index,tries,native);
   if(tries>=9){puts("GDC_DMA_UNCERTAIN_NO_FREE_NO_EXIT");for(;;)pause();}
  }
  hardware_time+=now()-start;start=now();
  if(cancelled||snprintf(tile_path,sizeof tile_path,"%s/render-tile-%u.jpg",argv[2],index)>=(int)sizeof tile_path)goto end;
  if(native_jpeg_trial_sized(ion,output,t->ow,t->oh,t->out_stride,t->out_stride*t->ph,ob))goto end;
  output_time+=now()-start;
  printf("GDC_TILE_DONE %u INPUT %.3f HARDWARE %.3f WRITE %.3f\n",index,input_time,hardware_time,output_time);
 }
#ifdef GDCF_RUNTIME
 if(fstat(gridfd,&after)||!same(&gridstat,&after)||lstat(argv[3],&after)||!same(&gridstat,&after))goto end;
#endif
 rc=0;
end:
 if(pending){puts("GDC_DMA_UNCERTAIN_NO_FREE_NO_EXIT");for(;;)pause();}
 if(map&&mapped_owner&&duss_hal_mem_unmap(mapped_owner))rc=3;
 if(ctx)gdc2_hal_close(ctx);
 if(grid&&duss_hal_mem_free(grid))rc=3;if(output&&duss_hal_mem_free(output))rc=3;if(input&&duss_hal_mem_free(input))rc=3;
 if(started&&duss_hal_device_stop(ion,NULL))rc=3;if(ion&&duss_hal_device_close(ion))rc=3;if(init&&duss_hal_deinitialize())rc=3;
 if(source>=0)close(source);if(file>=0&&close(file))rc=3;
 free(band);
#ifdef GDCF_RUNTIME
 if(gridfd>=0)close(gridfd);free(bundle);nfm_free(&mesh);hgp_free();
#endif
 if(rc&&owned&&unlink(argv[2]))puts("PRIVATE_FAILED_OUTPUT_RETAINED");
 printf("STREAM_GDC_JPEG_EXIT %d SECONDS %.3f INPUT %.3f HARDWARE %.3f WRITE %.3f\n",rc,now()-begin,input_time,hardware_time,output_time);return cancelled&&rc!=3?130:rc;
}
