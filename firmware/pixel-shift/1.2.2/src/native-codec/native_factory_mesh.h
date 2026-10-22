/* II1.3.16.2 native GDC v2 adapter. Optical coordinates must come from THIS
 * job's first-frame factory render, never a saved lens-independent table.
 * Geometry layout matches gdc_calc_playback in imx861bqr_hb722.conf. */
#ifndef NATIVE_FACTORY_MESH_H
#define NATIVE_FACTORY_MESH_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define NFM_COLS 184u
#define NFM_ROWS 138u
#define NFM_COUNT (NFM_COLS*NFM_ROWS)
typedef struct {float x,y;} NfmPoint;
typedef struct {uint64_t nonce;NfmPoint*points;unsigned char*present;unsigned completed,failed;} NativeFactoryMesh;
static inline uint32_t nfm_u32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void nfm_free(NativeFactoryMesh*m){if(m){free(m->points);free(m->present);memset(m,0,sizeof *m);}}
static inline int nfm_init(NativeFactoryMesh*m,uint64_t nonce){
 if(!m||!nonce)return 0;*m=(NativeFactoryMesh){.nonce=nonce};
 m->points=calloc(NFM_COUNT,sizeof *m->points);m->present=calloc(NFM_COUNT,1);
 if(!m->points||!m->present){nfm_free(m);return 0;}return 1;
}
static inline int nfm_add(NativeFactoryMesh*m,uint64_t nonce,unsigned index,const unsigned char*data,size_t bytes){
 if(!m||!m->points||!m->present||m->failed)return 0;
 if(nonce!=m->nonce||index>=8||m->completed&(1u<<index)||!data||bytes<128||data[0]!=2)goto fail;
 unsigned col=index%4,row=index/4,nx=col==3?39:48,ny=row?69:68;
 uint32_t size=nfm_u32(data+1),stride=nfm_u32(data+21),xo=nfm_u32(data+25),yo=nfm_u32(data+29);
 if(nfm_u32(data+5)!=64||nfm_u32(data+9)!=64||nfm_u32(data+13)!=nx||nfm_u32(data+17)!=ny||
    stride!=((col==3)?256:384)||size!=(ny+1)*stride||size>bytes-128||xo!=262144||yo!=262144)goto fail;
 unsigned dx=col*48,dy=row*68,sx=col>=2?3584:0,sy=row?550:0;
 for(unsigned y=0;y<=ny;y++)for(unsigned x=0;x<=nx;x++){
  const unsigned char*p=data+128+(size_t)y*stride+(x/5)*32;
  unsigned bit=(x%5)*25,byte=bit/8,shift=bit%8;
  uint32_t px=(nfm_u32(p+byte)>>shift)&0x7ffff,py=(nfm_u32(p+16+byte)>>shift)&0x7ffff;
  NfmPoint v={((int32_t)px-(int32_t)xo)/32.f+sx,((int32_t)py-(int32_t)yo)/32.f+sy};
  if(!isfinite(v.x)||!isfinite(v.y)||v.x<0||v.y<0||v.x>11656||v.y>8742)goto fail;
  size_t at=(size_t)(dy+y)*NFM_COLS+dx+x;if(at>=NFM_COUNT)goto fail;
  if(m->present[at]&&(m->points[at].x!=v.x||m->points[at].y!=v.y))goto fail;
  m->points[at]=v;m->present[at]=1;
 }
 m->completed|=1u<<index;return 1;
fail:m->failed=1;return 0;
}
static inline int nfm_complete(const NativeFactoryMesh*m,uint64_t nonce){
 if(!m||!nonce||m->nonce!=nonce||m->failed||m->completed!=255||!m->points||!m->present)return 0;
 for(size_t i=0;i<NFM_COUNT;i++)if(!m->present[i])return 0;return 1;
}
/* Active11656x8742 is NOT padded lattice11712x8768. Pixel-center convention
 * is explicit; sample only after nfm_complete. Scale both axes for407MP. */
static inline int nfm_sample(const NativeFactoryMesh*m,double x,double y,unsigned w,unsigned h,NfmPoint*out){
 if(!m||!m->points||m->failed||m->completed!=255||!out||!w||!h||!isfinite(x)||!isfinite(y)||x<0||y<0||x>w-1||y>h-1)return 0;
 double gx=((x+.5)*11656/w-.5)/64,gy=((y+.5)*8742/h-.5)/64;
 if(gx<0)gx=0;if(gy<0)gy=0;
 unsigned ix=(unsigned)gx,iy=(unsigned)gy;if(ix+1>=NFM_COLS||iy+1>=NFM_ROWS)return 0;
 double fx=gx-ix,fy=gy-iy;size_t i=(size_t)iy*NFM_COLS+ix;
 NfmPoint p[4]={m->points[i],m->points[i+1],m->points[i+NFM_COLS],m->points[i+NFM_COLS+1]};
 double a[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy},sx=0,sy=0;
 for(unsigned k=0;k<4;k++){sx+=a[k]*p[k].x;sy+=a[k]*p[k].y;}
 sx=(sx+.5)*w/11656-.5;sy=(sy+.5)*h/8742-.5;
 if(!isfinite(sx)||!isfinite(sy))return 0;
 /* Pixel-border replication only, never change global mesh coordinates. */
 if(sx<0)sx=0;if(sy<0)sy=0;if(sx>w-1)sx=w-1;if(sy>h-1)sy=h-1;
 *out=(NfmPoint){(float)sx,(float)sy};return 1;
}
#endif
