/* Native mirror-only transaction. Caller must own the request. No hardware I/O.
 * Only the two already validated positive-coordinate test origins are accepted.
 * Not a production request-identity mechanism or full-frame color solution. */
#ifndef NATIVE_LSC_REGISTER_TRANSACTION_H
#define NATIVE_LSC_REGISTER_TRANSACTION_H
#include <stdint.h>
typedef int (*NlrRead)(void*,unsigned,unsigned,uint32_t*);
typedef int (*NlrWrite)(void*,unsigned,unsigned,uint32_t);
static const unsigned nlr_ids[8]={6122,6124,6126,6128,6136,6138,6142,6144};
/* 1=changed and verified,0=not R2Y,-1=failed but restored,-2=restore failed. */
static int nlr_apply_xy(void*const windows[2],unsigned x,unsigned y,NlrRead read_reg,NlrWrite write_reg){
 if(!windows||!windows[0]||!windows[1]||!read_reg||!write_reg||x>=23310||y>=17482||(x&1)||(y&1))return -1;
 uint32_t before[2][4][8],after[2][4][8];unsigned zero=0;
 for(unsigned w=0;w<2;w++)for(unsigned c=0;c<4;c++)for(unsigned k=0;k<8;k++){
  if(read_reg(windows[w],nlr_ids[k],c,&before[w][c][k]))return -1;
  zero+=before[w][c][k]==0;
 }
 if(zero==64)return 0;
 for(unsigned w=0;w<2;w++)for(unsigned c=0;c<4;c++)for(unsigned k=0;k<8;k++){
  uint32_t expected=k>=4?128:(k%2?4371:(w?196:5828));
  if(before[w][c][k]!=expected)return -1;
  /* 17-bit center fields. Negative-center encoding is an explicit device
   * hypothesis until edge-window trials have passed; not production enabled. */
  int32_t center=k%2?8742-(int32_t)y:11656-(int32_t)x-(int32_t)w*5632;
  after[w][c][k]=k>=4?64:((uint32_t)center&0x1ffffu);
 }
 int failed=0;
 for(unsigned w=0;w<2&&!failed;w++)for(unsigned c=0;c<4&&!failed;c++)for(unsigned k=0;k<8;k++)
  if(write_reg(windows[w],nlr_ids[k],c,after[w][c][k])){failed=1;break;}
 for(unsigned w=0;w<2&&!failed;w++)for(unsigned c=0;c<4&&!failed;c++)for(unsigned k=0;k<8;k++){
  uint32_t value=0;
  if(read_reg(windows[w],nlr_ids[k],c,&value)||value!=after[w][c][k]){failed=1;break;}
 }
 if(!failed)return 1;
 int restored=1;
 for(unsigned w=0;w<2;w++)for(unsigned c=0;c<4;c++)for(unsigned k=0;k<8;k++)
  if(write_reg(windows[w],nlr_ids[k],c,before[w][c][k]))restored=0;
 for(unsigned w=0;w<2;w++)for(unsigned c=0;c<4;c++)for(unsigned k=0;k<8;k++){
  uint32_t value=0;if(read_reg(windows[w],nlr_ids[k],c,&value)||value!=before[w][c][k])restored=0;
 }
 return restored?-1:-2;
}
static int nlr_apply(void*const windows[2],unsigned origin,NlrRead r,NlrWrite w){
 if(origin!=0&&origin!=2048)return -1;
 return nlr_apply_xy(windows,origin,origin,r,w);
}
#endif
