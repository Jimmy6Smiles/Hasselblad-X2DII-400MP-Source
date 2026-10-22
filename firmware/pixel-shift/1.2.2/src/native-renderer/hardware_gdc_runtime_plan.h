/* Per-job planner for the tested 407 MP NV16 band path. No saved lens grid. */
#ifndef HARDWARE_GDC_RUNTIME_PLAN_H
#define HARDWARE_GDC_RUNTIME_PLAN_H
#include "native_factory_grid_bundle.h"
#define GDCF_BAND_ROWS 512u
#define TILE_COUNT 210u
typedef struct{int sx,sy;unsigned dx,dy,ow,oh,pw,ph,stride,rows,out_stride,bytes;const unsigned char*grid;}GdcTile;
static GdcTile tiles[TILE_COUNT];
static unsigned GDCF_INPUT,GDCF_OUTPUT,GDCF_GRID;
static unsigned hgp_align(unsigned x,unsigned n){return (x+n-1)/n*n;}
static void hgp_free(void){
 for(unsigned i=0;i<TILE_COUNT;i++){free((void*)tiles[i].grid);memset(&tiles[i],0,sizeof tiles[i]);}
 GDCF_INPUT=GDCF_OUTPUT=GDCF_GRID=0;
}
static int hgp_sample(const NativeFactoryMesh*m,unsigned x,unsigned y,NfmPoint*out){
 double gx=fmax(0,((x+.5)*11656/23310-.5)/64),gy=fmax(0,((y+.5)*8742/17482-.5)/64);
 if(gx>NFM_COLS||gy>NFM_ROWS)return 0;
 unsigned ix=(unsigned)gx,iy=(unsigned)gy;
 if(ix>NFM_COLS-2)ix=NFM_COLS-2;if(iy>NFM_ROWS-2)iy=NFM_ROWS-2;
 double fx=gx-ix,fy=gy-iy;
 size_t i=(size_t)iy*NFM_COLS+ix;
 NfmPoint p[4]={m->points[i],m->points[i+1],m->points[i+NFM_COLS],m->points[i+NFM_COLS+1]};
 double a[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy},sx=0,sy=0;
 for(unsigned k=0;k<4;k++){sx+=a[k]*p[k].x;sy+=a[k]*p[k].y;}
 sx=(sx+.5)*23310/11656-.5;sy=(sy+.5)*17482/8742-.5;
 if(!isfinite(sx)||!isfinite(sy)||fabs(sx)>32768||fabs(sy)>32768)return 0;
 *out=(NfmPoint){(float)sx,(float)sy};return 1;
}
static int hgp_make(const NativeFactoryMesh*m,uint64_t nonce){
 if(!nfm_complete(m,nonce)||GDCF_INPUT||GDCF_OUTPUT||GDCF_GRID)return 0;
 unsigned index=0;uint64_t coverage=0;
 for(unsigned y=0;y<17482;y+=512)for(unsigned x=0;x<23310;x+=4096){
  if(index>=TILE_COUNT)goto fail;
  GdcTile*t=&tiles[index++];t->dx=x;t->dy=y;t->ow=23310-x<4096?23310-x:4096;t->oh=17482-y<512?17482-y:512;
  t->pw=hgp_align(t->ow,64);t->ph=hgp_align(t->oh,64);t->out_stride=hgp_align(t->pw,256);
  unsigned nx=t->pw/64,ny=t->ph/64;
  NfmPoint points[9*65];double minx=INFINITY,miny=INFINITY,maxx=-INFINITY,maxy=-INFINITY;
  for(unsigned j=0;j<=ny;j++)for(unsigned i=0;i<=nx;i++){
   NfmPoint*p=&points[j*(nx+1)+i];if(!hgp_sample(m,x+i*64,y+j*64,p))goto fail;
   minx=fmin(minx,p->x);miny=fmin(miny,p->y);maxx=fmax(maxx,p->x);maxy=fmax(maxy,p->y);
  }
  t->sx=(int)(floor((floor(minx)-64)/2)*2);t->sy=(int)floor(miny)-64;
  int iw=(int)ceil(maxx)+65-t->sx,ih=(int)ceil(maxy)+65-t->sy;
  if(iw<=0||ih<=0||iw>8192||ih>8192||t->sx < -8192||t->sx>32768||t->sy < -8192||t->sy>32768)goto fail;
  t->stride=hgp_align((unsigned)iw,256);t->rows=hgp_align((unsigned)ih,64);
  if(t->stride>8192||t->rows>8192)goto fail;
  unsigned gs=hgp_align(((nx+5)/5)*32,128);t->bytes=128+(ny+1)*gs;
  unsigned char*grid=calloc(1,t->bytes);if(!grid)goto fail;t->grid=grid;grid[0]=2;
  unsigned header[8]={t->bytes-128,64,64,nx,ny,gs,262144,262144};
  for(unsigned k=0;k<8;k++)nfgb_put32(grid+1+4*k,header[k]);
  for(unsigned j=0;j<=ny;j++)for(unsigned i=0;i<=nx;i++)for(unsigned axis=0;axis<2;axis++){
   NfmPoint*p=&points[j*(nx+1)+i];double v=axis?(double)p->y-t->sy:(double)p->x-t->sx;
   long q=(long)floor(v*32+.5)+262144;
   if(q<0||q>0x7ffff||v<0||v>(axis?ih:iw)-1)goto fail;
   unsigned bit=(i%5)*25;unsigned char*dst=grid+128+j*gs+(i/5)*32+axis*16;
   for(unsigned b=0;b<19;b++)if((unsigned long)q&(1ul<<b))dst[(bit+b)/8]|=(unsigned char)(1u<<((bit+b)%8));
  }
  unsigned ib=t->stride*t->rows*2,ob=t->out_stride*t->ph*2,gb=hgp_align(t->bytes,4096);
  if(ib>GDCF_INPUT)GDCF_INPUT=ib;if(ob>GDCF_OUTPUT)GDCF_OUTPUT=ob;if(gb>GDCF_GRID)GDCF_GRID=gb;
  coverage+=(uint64_t)t->ow*t->oh;
 }
 if(index!=TILE_COUNT||coverage!=UINT64_C(23310)*17482)goto fail;
 /* Hard bound rather than adapting allocations beyond the proven memory budget. */
 if((uint64_t)GDCF_INPUT+GDCF_OUTPUT+GDCF_GRID>32u*1024*1024)goto fail;
 return 1;
fail:hgp_free();return 0;
}
#endif
