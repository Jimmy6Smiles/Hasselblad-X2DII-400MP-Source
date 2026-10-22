/* Experimental native-domain row provider. Include after overlap_six_worker.c.
 * Reuses the established six-frame sampling/kernel, NOT its normalized LUT.
 * No output files, no camera API, no source deletion, no full-image allocation. */
#ifndef NATIVE_MERGE_ROWS_H
#define NATIVE_MERGE_ROWS_H
#include "native_raw_domain.h"
typedef struct {
 Job job;
 void *scratch;
 uint16_t (*a)[4],(*b)[4],(*cur)[3],(*prev)[3],*output,*row;
 uint8_t *cv,*pv;
 unsigned cached_pair;
} NativeMergeRows;
static void nmr_close(NativeMergeRows *n){
 if(!n)return;
 for(unsigned i=0;i<6;i++){
  Frame*f=n->job.frame+i;if(f->f)fclose(f->f);free(f->rows);free(f->lut);
 }
 free(n->scratch);memset(n,0,sizeof *n);
}
static int nmr_open(NativeMergeRows *n,const char *jobdir,const NativeRawRange *target){
 if(!n||!jobdir||!nrd_valid(target))return 0;
 /* Native observed metadata repeats one scalar black value across CFA sites.
  * Mixing the two green positions needs explicit handling for other layouts. */
 for(unsigned c=1;c<4;c++)if(target->black[c]!=target->black[0])return 0;
 memset(n,0,sizeof *n);n->cached_pair=UINT32_MAX;
 if(!load(&n->job,jobdir))goto fail;
 for(unsigned i=0;i<6;i++){
  Frame*f=n->job.frame+i;NativeRawRange source={{0},f->white};
  memcpy(source.black,f->black,sizeof source.black);
  if(!nrd_valid(&source))goto fail;
  f->lut=malloc(4u*65536u*sizeof *f->lut);if(!f->lut)goto fail;
  for(unsigned c=0;c<4;c++)for(unsigned v=0;v<65536;v++)
   if(cancelled||!nrd_sample(v,c,&source,target,f->lut+c*65536u+v))goto fail;
 }
 unsigned w=n->job.cw-1;
 n->scratch=calloc(1,(size_t)w*38+n->job.cw*2);if(!n->scratch)goto fail;
 unsigned char*p=n->scratch;
 n->a=(void*)p;p+=w*8;n->b=(void*)p;p+=w*8;
 n->cur=(void*)p;p+=w*6;n->prev=(void*)p;p+=w*6;
 n->output=(void*)p;p+=w*8;n->row=(void*)p;p+=n->job.cw*2;
 n->cv=p;p+=w;n->pv=p;return 1;
fail:nmr_close(n);return 0;
}
static int nmr_read_row(void*context,unsigned row,uint16_t*out,unsigned width){
 NativeMergeRows*n=context;if(!n||!out||!n->scratch||cancelled)return 0;
 unsigned w=n->job.cw-1,h=n->job.ch-1,y=row/2;
 if(width!=2*w||y>=h)return 0;
 if(n->cached_pair!=y){
  if(!integer_row(&n->job,y,n->a,n->row)||
     !integer_row(&n->job,y+1<h?y+1:y,n->b,n->row)||
     !half_row(&n->job,y,n->cur,n->cv,n->row)||
     !half_row(&n->job,y?y-1:0,n->prev,n->pv,n->row))return 0;
  SixNativeRow ctx={w,(const uint16_t(*)[4])n->a,(const uint16_t(*)[4])n->b,
   (const uint16_t(*)[3])n->cur,(const uint16_t(*)[3])n->prev,n->cv,n->pv,n->output};
  six_native_range(0,w,&ctx);n->cached_pair=y;
 }
 memcpy(out,n->output+(row&1)*2*w,width*sizeof *out);return 1;
}
#endif
