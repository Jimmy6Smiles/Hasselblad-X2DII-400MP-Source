/* Bounded incremental exact reconstruction of the first 128 source rows.
 * Uses 24 MiB accumulator, not six full frames; no storage-card scratch.
 * Caller supplies normalized cropped rows from the existing parser. */
#ifndef PS_OVERLAP_PREFIX_H
#define PS_OVERLAP_PREFIX_H
#include "six_row_kernel.h"
#include "readback_digest.h"
#define OP_ROWS 128u
#define OP_WIDTH 11655u
typedef struct {uint32_t g,r,b;uint16_t half_g;uint8_t nr,nb;} OpPixel;
typedef struct {
 char magic[8];uint32_t width,rows;uint64_t identities[6][5];
 unsigned char job[256];PsReadbackDigest digest;
} OpMeta;
static unsigned op_channel(unsigned x,unsigned y){static const unsigned c[2][2]={{0,1},{3,2}};return c[y&1][x&1];}
static uint16_t op_normalize(unsigned v,unsigned black,unsigned white){
 (void)black;(void)white;return (uint16_t)v;
}
static void op_identity(uint64_t out[5],const struct stat *s){
 out[0]=s->st_dev;out[1]=s->st_ino;out[2]=s->st_size;
 out[3]=(uint64_t)s->st_mtim.tv_sec*1000000000+s->st_mtim.tv_nsec;
 out[4]=(uint64_t)s->st_ctim.tv_sec*1000000000+s->st_ctim.tv_nsec;
}
static void op_add_integer(OpPixel *p,unsigned y,unsigned x,unsigned c,unsigned v){
 if(c==1||c==3){if(y<OP_ROWS)p[(size_t)y*OP_WIDTH+x].g+=v;}
 else if(c==0&&y<OP_ROWS){
  p[(size_t)y*OP_WIDTH+x].r+=v;
  if(x)p[(size_t)y*OP_WIDTH+x-1].r+=v;
  if(x==OP_WIDTH-1)p[(size_t)y*OP_WIDTH+x].r+=v;
 }else if(c==2){
  if(y<OP_ROWS)p[(size_t)y*OP_WIDTH+x].b+=v;
  if(y)p[(size_t)(y-1)*OP_WIDTH+x].b+=v;
 }
}
static void op_add_half(OpPixel *p,unsigned y,unsigned x,unsigned c,unsigned v){
 OpPixel *a=p+(size_t)y*OP_WIDTH+x;
 if(c==1||c==3)a->half_g=(uint16_t)v;
 else if(c==0){
  a->r+=v;a->nr++;
  if(y+1<OP_ROWS){a[OP_WIDTH].r+=v;a[OP_WIDTH].nr++;}
  if(!y){a->r+=v;a->nr++;}
 }else{
  a->b+=v;a->nb++;
  if(x+1<OP_WIDTH){a[1].b+=v;a[1].nb++;}
  if(!x){a->b+=v;a->nb++;}
 }
}
static void op_render(const OpPixel *p,unsigned y,uint16_t *out){
 p+=(size_t)y*OP_WIDTH;
 for(unsigned x=0;x<OP_WIDTH;x++){
  out[2*x]=(uint16_t)((p[x].g+1)/2);
  out[2*x+1]=six_native_mean(p[x].r,2+p[x].nr);
  out[2*OP_WIDTH+2*x]=six_native_mean(p[x].b,2+p[x].nb);
  out[2*OP_WIDTH+2*x+1]=p[x].half_g;
 }
}
#endif
