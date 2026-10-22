/* RAM-only native encoder test. No output file, album or capture operations. */
#include "native_jpeg_layout.h"
#ifndef NATIVE_JPEG_QUALITY
#define NATIVE_JPEG_QUALITY 90
#endif
#ifndef NATIVE_JPEG_CAPACITY
#define NATIVE_JPEG_CAPACITY (64u*1024u*1024u)
#endif
extern int duss_hal_ienc_encfrm(void*,void*);
static int native_jpeg_trial_sized(void*ion,void*input,uint32_t width,uint32_t height,uint32_t stride,uint32_t uv,uint32_t allocation){
 void*out=NULL,*enc=NULL,*mapped=NULL;int rc=60;const uint32_t cap=NATIVE_JPEG_CAPACITY;
 unsigned char frame[NJ_FRAME_BYTES],stream[NJ_STREAM_BYTES];
 if(duss_hal_mem_alloc(ion,&out,cap,4096,2,0)||!out)goto end;
 if(!nj_layout(frame,stream,(uintptr_t)input,(uintptr_t)out,width,height,stride,uv,allocation,cap))goto end;
 if(duss_hal_device_open("/dev/ienc0",NULL,&enc)||!enc)goto end;
 struct {void*frame;void*stream;uint32_t quality,reserved;} request={frame,stream,NATIVE_JPEG_QUALITY,0};
 puts("NATIVE_JPEG_ENCODE_BEGIN");double begin=now();
 int status=duss_hal_ienc_encfrm(enc,&request);
 printf("NATIVE_JPEG_RETURN %d SECONDS %.3f\n",status,now()-begin);rc=61;
 if(status)goto end;
 uint32_t off=njget(stream,0x3c),extra=njget(stream,0x44),len=njget(stream,0x40);
 if(off>cap||extra>cap-off||len<4||len>cap-off-extra)goto end;
 if(duss_hal_mem_map(out,&mapped)||!mapped||duss_hal_mem_sync(out,1))goto end;
 const unsigned char*p=(const unsigned char*)mapped+off+extra;
 if(p[0]!=255||p[1]!=216||p[len-2]!=255||p[len-1]!=217)goto end;
 /* Parse bounded JPEG marker headers to verify actual encoded dimensions. */
 uint32_t pos=2;int matched=0;
 while(pos+4<=len){
  if(p[pos++]!=255)break;
  while(pos<len&&p[pos]==255)pos++;
  if(pos>=len)break;unsigned marker=p[pos++];
  if(marker==0xda||marker==0xd9)break;
  if(marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
  if(pos+2>len)break;uint32_t n=(uint32_t)p[pos]*256+p[pos+1];
  if(n<2||n>len-pos)break;
  if(marker==0xc0||marker==0xc1||marker==0xc2){
   if(n<8)break;unsigned h=(unsigned)p[pos+3]*256+p[pos+4],w=(unsigned)p[pos+5]*256+p[pos+6];
   matched=w==width&&h==height&&p[pos+2]==8;
   printf("NATIVE_JPEG_DIMENSIONS %u %u BYTES %u\n",w,h,len);break;
  }
  pos+=n;
 }
 if(matched){
  rc=0;
#ifdef NATIVE_JPEG_VERIFY
  if(!native_verify_jpeg(p,len,width,height))rc=63;
#endif
 }
end:
 if(mapped&&duss_hal_mem_unmap(out))rc=62;
 if(enc&&duss_hal_device_close(enc))rc=62;
 if(out&&duss_hal_mem_free(out))rc=62;
 printf("NATIVE_JPEG_EXIT %d NO_PHOTO_FILE_WRITES\n",rc);return rc;
}
static int native_jpeg_trial(void*ion,void*input){return native_jpeg_trial_sized(ion,input,11656,8742,11776,103251968,206503936);}
