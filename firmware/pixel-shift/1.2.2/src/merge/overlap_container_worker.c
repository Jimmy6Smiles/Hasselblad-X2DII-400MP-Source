/* Offline candidate: existing reconstruction -> final padded RAW and preview.
 * No camera control/album/delete API. Never removes source frames. */
#define _GNU_SOURCE
#include <stdio.h>
#include "readback_digest.h"
static int direct_write(FILE*,PsReadbackDigest*,const void*,size_t);
static unsigned overlap_skip;
#define main unused_legacy_main
#include "overlap_six_worker.c"
#undef main
#define PS_ALBUM_LIBRARY
#define cancelled album_cancelled
#include "album_candidate.c"
#undef cancelled
#include "direct_header.h"
#include "final_header.h"
#include "overlap_prefix.h"
int ps_prepare_six(const char *path);
int ps_prepare_cancelled(void){return cancelled!=0;}

typedef struct {
 unsigned y,py,black,white;uint64_t sums[1920][3];double matrix[9],wb[3];
 unsigned char rgb[1920*3];unsigned char *padded;
 struct jpeg_compress_struct jpeg;JError error;int created,started;
} DirectSink;
static DirectSink sink;
static int start_preview(FILE *jpg){
 sink.jpeg.err=jpeg_std_error(&sink.error.pub);sink.error.pub.error_exit=jpeg_error;
 if(setjmp(sink.error.jump))return 0;
 sink.created=1;jpeg_create_compress(&sink.jpeg);jpeg_stdio_dest(&sink.jpeg,jpg);
 sink.jpeg.image_width=1920;sink.jpeg.image_height=1440;sink.jpeg.input_components=3;sink.jpeg.in_color_space=JCS_RGB;
 jpeg_set_defaults(&sink.jpeg);jpeg_set_quality(&sink.jpeg,90,TRUE);jpeg_start_compress(&sink.jpeg,TRUE);sink.started=1;
 return 1;
}
static int finish_preview(void){
 if(setjmp(sink.error.jump))return 0;
 jpeg_finish_compress(&sink.jpeg);jpeg_destroy_compress(&sink.jpeg);sink.created=0;return 1;
}
static int emit_preview(void){
 unsigned ny=2*((uint64_t)(sink.py+1)*8741/1440)-2*((uint64_t)sink.py*8741/1440);
 for(unsigned x=0;x<1920;x++){
  unsigned nx=2*((uint64_t)(x+1)*11655/1920)-2*((uint64_t)x*11655/1920);
  unsigned count=(nx/2)*(ny/2);double camera[3];
  if(!count)return 0;
  for(unsigned k=0;k<3;k++)camera[k]=((double)sink.sums[x][k]/(k==1?2*count:count)-sink.black)/(sink.white-sink.black)*sink.wb[k];
  for(unsigned k=0;k<3;k++)sink.rgb[3*x+k]=gamma_byte(sink.matrix[3*k]*camera[0]+sink.matrix[3*k+1]*camera[1]+sink.matrix[3*k+2]*camera[2]);
 }
 if(setjmp(sink.error.jump))return 0;
 JSAMPROW row=sink.rgb;
 if(jpeg_write_scanlines(&sink.jpeg,&row,1)!=1)return 0;
 memset(sink.sums,0,sizeof sink.sums);sink.py++;return 1;
}
static int exact_write(FILE *out,const void *data,size_t bytes){
 const unsigned char *p=data;int fd=fileno(out);
 while(bytes){
  if(cancelled)return 0;
  ssize_t n=write(fd,p,bytes);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  p+=n;bytes-=(size_t)n;
 }
 return 1;
}
static int direct_write(FILE *out,PsReadbackDigest *digest,const void *data,size_t bytes){
 const unsigned width=23310,stride=23312*2;
 if(bytes%(width*4)||bytes>(size_t)PS_CHUNK_ROWS*width*4)return 0;
 unsigned rows=(unsigned)(bytes/(width*2));const unsigned char *src=data;
 if(sink.y+rows>17482||cancelled)return 0;
 double stage_start=prof_now();
 for(unsigned r=0;r<rows;r++){
  unsigned char *dst=sink.padded+(size_t)r*stride;const unsigned char *p=src+(size_t)r*width*2;
  memcpy(dst,p+2,2);memcpy(dst+2,p,width*2);memcpy(dst+2+width*2,p+width*2-4,2);
 }
 prof_pad+=prof_now()-stage_start;stage_start=prof_now();
 if(!exact_write(out,sink.padded,(size_t)rows*stride))return 0;
 prof_write+=prof_now()-stage_start;stage_start=prof_now();
 (void)digest; /* No output-raster digest. Prefix-cache digest remains independent. */
 sink.y+=rows;
 return 1;
}
static int direct_same(const struct stat *a,const struct stat *b){
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static unsigned char *load_prefix(const char *task,const struct stat ids[6]){
 int d=open(task,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return NULL;
 int fd=-1;unsigned char *data=NULL;OpMeta meta;struct stat st;
 fd=openat(d,"prefix.meta",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=sizeof meta||read(fd,&meta,sizeof meta)!=sizeof meta)goto fail;
 close(fd);fd=-1;
 if(memcmp(meta.magic,"PSPFX01",8)||meta.rows!=OP_ROWS||meta.width!=OP_WIDTH)goto fail;
 for(unsigned i=0;i<6;i++){uint64_t id[5];op_identity(id,ids+i);if(memcmp(id,meta.identities[i],sizeof id))goto fail;}
 unsigned char job[256];fd=openat(d,"job.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0||read(fd,job,sizeof job)!=sizeof job||memcmp(job,meta.job,sizeof job))goto fail;
 close(fd);fd=-1;
 const size_t bytes=(size_t)OP_ROWS*OP_WIDTH*8;
 fd=openat(d,"prefix.raw",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)bytes)goto fail;
 data=malloc(bytes);if(!data)goto fail;
 size_t pos=0;while(pos<bytes){ssize_t n=read(fd,data+pos,bytes-pos);if(cancelled||n<=0)goto fail;pos+=(size_t)n;}
 PsReadbackDigest digest;ps_digest_init(&digest);ps_digest_update(&digest,data,bytes);
 if(!ps_digest_equal(&digest,&meta.digest))goto fail;
 close(fd);close(d);return data;
fail:
 if(fd>=0)close(fd);close(d);free(data);return NULL;
}
static int direct_run(const char *task,const char *target){
 Job j={0};FILE *out=NULL,*template=NULL,*jpg=NULL,*meta=NULL;
 unsigned char *dh=NULL,*header=NULL,*jpeg=NULL,*cached=NULL;unsigned ds=0,hs=0;
 FmIfd raw={0};struct stat before[6],now;int rc=1,owned=0,dir=-1;
 char partial[4096]={0},path[4096],parent[4096];double start=seconds(),rows_end=0,verify_end=0;
 memset(&sink,0,sizeof sink);
 overlap_skip=0;
 if(target[0]!='/'||strlen(target)>3800||strstr(target,"/../"))goto end;
 if(!lstat(target,&now)||errno!=ENOENT)goto end;
 snprintf(partial,sizeof partial,"%s.partial",target);strcpy(parent,target);
 char *slash=strrchr(parent,'/');if(!slash||slash==parent)goto end;*slash=0;
 dir=open(parent,O_DIRECTORY|O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)goto end;
 if(!load(&j,task)||j.cw!=11656||j.ch!=8742||!prepare_luts(&j))goto end;
 sink.black=j.frame[0].black[0];sink.white=j.frame[0].white;
 for(unsigned i=0;i<6;i++)if(fstat(fileno(j.frame[i].f),before+i))goto end;
 cached=load_prefix(task,before);
 printf("PIXEL_PREFIX_SELECTION cached=%d at=%.6f\n",cached!=NULL,seconds());
 if(!path_join(path,task,"header.dng")||!(template=fopen(path,"rb")))goto end;
 dh=first_frame_header(j.frame[0].f,j.frame[0].bytes,template,j.header,23310,17482,&ds);
 if(!dh)goto end;fclose(template);template=NULL;
 j.header=ds;if(!validate_header(dh,&j))goto end;
 header=direct_header(j.frame[0].f,dh,ds,0,&hs);if(!header)goto end;
 unsigned joff=0,jn=0,final_size=0;
 unsigned char *final=final_header(j.frame[0].f,header,hs,&final_size,&joff,&jn);
 if(!final)goto end;free(header);header=final;hs=final_size;
 free(dh);dh=NULL;
 jpeg=malloc(jn);if(!jpeg||!fm_read(j.frame[0].f,j.frame[0].bytes,joff,jpeg,jn))goto end;
 sink.padded=malloc((size_t)PS_CHUNK_ROWS*2*23312*2);if(!sink.padded)goto end;
 out=fopen(partial,"wbx+");if(!out)goto end;owned=1;
 if(!exact_write(out,header,hs))goto end;
 double setup_end=prof_now();
 PsReadbackDigest written;ps_digest_init(&written);
 if(cached){
  for(unsigned y=0;y<OP_ROWS;y+=PS_CHUNK_ROWS){
   unsigned n=OP_ROWS-y;if(n>PS_CHUNK_ROWS)n=PS_CHUNK_ROWS;
   if(!direct_write(out,&written,cached+(size_t)y*OP_WIDTH*8,(size_t)n*OP_WIDTH*8))goto end;
  }
  free(cached);cached=NULL;overlap_skip=OP_ROWS;
  printf("PIXEL_PREFIX_REUSED rows=%u NO_REPEAT_RECONSTRUCTION\n",overlap_skip);
 }
 if(!chunk_pipeline(&j,out,&written)||sink.y!=17482)goto end;
 double pipeline_end=prof_now();
 if(!exact_write(out,jpeg,jn))goto end;
 unsigned pad=(4096-(unsigned)jn%4096)%4096;unsigned char zeros[4096]={0};
 if(!exact_write(out,zeros,pad)||!inline_set(header,hs,fm32(header+4),279,4,1,((unsigned)jn+4095)&~4095u))goto end;
 if(lseek(fileno(out),0,SEEK_SET)<0||!exact_write(out,header,hs))goto end;
 double tail_end=prof_now();if(fflush(out))goto end;double flush_end=prof_now();
 if(fsync(fileno(out)))goto end;double sync_end=prof_now();
 for(unsigned i=0;i<6;i++)if(fstat(fileno(j.frame[i].f),&now)||!direct_same(before+i,&now))goto end;
 /* Release reconstruction buffers before lightweight header/JPEG checks. */
 for(unsigned i=0;i<6;i++){fclose(j.frame[i].f);j.frame[i].f=NULL;free(j.frame[i].lut);j.frame[i].lut=NULL;free(j.frame[i].rows);j.frame[i].rows=NULL;}
 free(sink.padded);sink.padded=NULL;
 {int e=fclose(out);out=NULL;if(e)goto end;}
 rows_end=seconds();out=fopen(partial,"rb");if(!out)goto end;
 unsigned char block[65536];if(hs>sizeof block||fread(block,1,hs,out)!=hs||memcmp(block,header,hs))goto end;
 double rb_read=0,rb_digest=0;
 /* No raster reread. Retain exact length, header and JPEG-tail checks. */
 struct stat output_stat;
 uint64_t raster_end=(uint64_t)hs+UINT64_C(23312)*17482*2;
 if(fstat(fileno(out),&output_stat)||!S_ISREG(output_stat.st_mode)||output_stat.st_nlink!=1||
    output_stat.st_size!=(off_t)(raster_end+jn+pad)||fseeko(out,(off_t)raster_end,SEEK_SET)||cancelled)goto end;
 for(size_t pos=0;pos<(size_t)jn;){size_t n=(size_t)jn-pos;if(n>sizeof block)n=sizeof block;
  if(cancelled||fread(block,1,n,out)!=n||memcmp(block,jpeg+pos,n))goto end;pos+=n;
 }
 if(fread(block,1,pad,out)!=pad||memcmp(block,zeros,pad)||fgetc(out)!=EOF||ferror(out)||cancelled)goto end;
 {int e=fclose(out);out=NULL;if(e)goto end;}verify_end=seconds();
 if(syscall(SYS_renameat2,AT_FDCWD,partial,AT_FDCWD,target,1))goto end;owned=0;
 if(fsync(dir))goto end;
 printf("TIMING_SERIAL setup=%.6f pipeline=%.6f writer_wait=%.6f padding=%.6f fwrite=%.6f digest=%.6f tail=%.6f fflush=%.6f fsync=%.6f release_close=%.6f readback_read=%.6f readback_digest=%.6f\n",setup_end-start,pipeline_end-setup_end,prof_writer_wait,prof_pad,prof_write,prof_digest,tail_end-pipeline_end,flush_end-tail_end,sync_end-flush_end,rows_end-sync_end,rb_read,rb_digest);
 struct rusage usage={0};getrusage(RUSAGE_SELF,&usage);
 printf("SINGLE_PASS_CONTAINER_IO_CHECK_PASS_NO_RASTER_READBACK seconds=%.3f merge_preview_write=%.3f readback=%.3f maxrss_kib=%ld\n",seconds()-start,rows_end-start,verify_end-rows_end,usage.ru_maxrss);rc=0;
end:
 if(sink.created)jpeg_destroy_compress(&sink.jpeg);
 if(out)fclose(out);if(template)fclose(template);if(meta)fclose(meta);if(jpg)fclose(jpg);if(dir>=0)close(dir);
 for(unsigned i=0;i<6;i++){if(j.frame[i].f)fclose(j.frame[i].f);free(j.frame[i].lut);free(j.frame[i].rows);}
 free(dh);free(header);free(jpeg);free(cached);free(sink.padded);fm_free(&raw);
 if(owned)unlink(partial);
 if(rc)fprintf(stderr,"DIRECT_FAILED cancelled=%d errno=%d\n",cancelled,errno);
 return cancelled?130:rc;
}
int main(int argc,char **argv){
 if(argc!=4||(strcmp(argv[1],"--auto-six")&&strcmp(argv[1],"--prepared-six"))||argv[2][0]!='/'||argv[3][0]!='/')return 2;
 signal(SIGTERM,on_signal);signal(SIGINT,on_signal);
 struct stat st;if(!lstat(argv[3],&st)||errno!=ENOENT)return 2;
 if(!strcmp(argv[1],"--auto-six")){int r=ps_prepare_six(argv[2]);if(r||cancelled)return cancelled?130:r;}
 return direct_run(argv[2],argv[3]);
}
