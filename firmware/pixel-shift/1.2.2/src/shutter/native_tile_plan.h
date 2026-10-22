/* Bounded 407MP coverage for the verified 11656x8742 native output window.
 * Keep seams deep inside overlapping windows. No allocation or device calls.
 * Geometry only: this is not evidence of whole-image color acceptance. */
#ifndef NATIVE_TILE_PLAN_H
#define NATIVE_TILE_PLAN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum {NTP_WIDTH=23310,NTP_HEIGHT=17482,NTP_TILE_WIDTH=11656,NTP_TILE_HEIGHT=8742,NTP_COUNT=9};
typedef struct {unsigned serial,x,y,left,top,right,bottom;} NativeTile;
static inline int ntp_get(unsigned index,NativeTile*out){
 static const unsigned xs[3]={0,8192,11654},ys[3]={0,6144,8740};
 static const unsigned xc[4]={0,9924,15750,NTP_WIDTH},yc[4]={0,7442,11812,NTP_HEIGHT};
 if(!out||index>=NTP_COUNT)return 0;
 unsigned x=index%3,y=index/3;
 *out=(NativeTile){index+1,xs[x],ys[y],xc[x],yc[y],xc[x+1],yc[y+1]};return 1;
}
static inline int ntp_valid(const NativeTile*t){
 NativeTile expected;
 return t&&t->serial&&ntp_get(t->serial-1,&expected)&&
  t->x==expected.x&&t->y==expected.y&&t->left==expected.left&&t->top==expected.top&&
  t->right==expected.right&&t->bottom==expected.bottom;
}
/* Copy only this tile's assigned segment of one full-resolution NV16 row.
 * Caller supplies a reusable row pair rather than an 815 MB output surface. */
static inline int ntp_copy_row(const NativeTile*t,unsigned y,const unsigned char*src,size_t bytes,
 unsigned stride,size_t uv_offset,unsigned char*luma,unsigned char*chroma,size_t row_bytes){
 if(!ntp_valid(t)||!src||!luma||!chroma||y<t->top||y>=t->bottom||row_bytes<NTP_WIDTH||
    stride<NTP_TILE_WIDTH||uv_offset<(uint64_t)stride*NTP_TILE_HEIGHT||
    uv_offset>bytes||(uint64_t)stride*NTP_TILE_HEIGHT>bytes-uv_offset)return 0;
 size_t offset=(size_t)(y-t->y)*stride+t->left-t->x,n=t->right-t->left;
 memcpy(luma+t->left,src+offset,n);memcpy(chroma+t->left,src+uv_offset+offset,n);return 1;
}
#endif
