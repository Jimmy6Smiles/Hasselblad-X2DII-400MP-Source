/* Copy the first frame's authenticated root JPEG; no decode or re-encode. */
#define _GNU_SOURCE
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "first_frame_metadata.h"
static int number(FmIfd*d,unsigned id,uint32_t*v){FmTag*t=fm_find(d,id);if(!t||t->count!=1||(t->type!=3&&t->type!=4))return 0;*v=t->type==3?fm16(t->data):fm32(t->data);return 1;}
/* Walk JPEG structure, not byte-search: APP payloads can contain false EOI.
 * Factory strip alignment bytes after the real EOI need not be initialized. */
static int jpeg_end(FILE*f,uint32_t off,uint32_t count,uint32_t*length){
 if(fseeko(f,off,SEEK_SET))return 0;uint32_t at=0;int entropy=0,c;
#define NEXT() (at<count ? (at++,fgetc(f)) : EOF)
 if(NEXT()!=255||NEXT()!=216)return 0;
 while(at<count){
  c=NEXT();if(c==EOF)return 0;
  if(c!=255){if(entropy)continue;return 0;}
  do{c=NEXT();}while(c==255);
  if(c==EOF)return 0;
  if(entropy&&(c==0||(c>=208&&c<=215)))continue;
  if(c==217){*length=at;return count-at<=4095;}
  if(c==0||c==216||(c>=208&&c<=215))return 0;
  if(c==1)continue;
  int hi=NEXT(),lo=NEXT();if(hi<0||lo<0)return 0;
  unsigned n=(unsigned)hi*256+lo;if(n<2||n-2>count-at)return 0;
  if(fseeko(f,n-2,SEEK_CUR))return 0;at+=n-2;entropy=c==218;
 }
 return 0;
#undef NEXT
}
int main(int argc,char**argv){
 if(argc!=3)return 2;
 int fd=-1,out=-1,owned=0,rc=1;FILE*f=NULL;FmIfd d={0};struct stat a,b;unsigned char h[8],buf[65536];
 uint32_t w=0,height=0,off=0,n=0,compression=0;unsigned ids[]={256,257,259,273,279};
 fd=open(argv[1],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0||fstat(fd,&a)||!S_ISREG(a.st_mode))goto end;
 f=fdopen(fd,"rb");if(!f)goto end;fd=-1;
 if(!fm_read(f,a.st_size,0,h,8)||memcmp(h,"II*\0",4)||!fm_parse(f,a.st_size,fm32(h+4),&d,ids,5)||
 !number(&d,256,&w)||!number(&d,257,&height)||!number(&d,259,&compression)||
 !number(&d,273,&off)||!number(&d,279,&n)||w!=3888||height!=2918||(compression!=6&&compression!=7)||n<4||n>16000000||
 !fm_read(f,a.st_size,off,h,2)||h[0]!=255||h[1]!=216)goto end;
 uint32_t length=0;if((uint64_t)off+n>(uint64_t)a.st_size||!jpeg_end(f,off,n,&length))goto end;
 unsigned padding=n-length;n=length;
 out=open(argv[2],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(out<0)goto end;owned=1;
 for(uint32_t at=0;at<n;){size_t k=n-at;if(k>sizeof buf)k=sizeof buf;if(!fm_read(f,a.st_size,(uint64_t)off+at,buf,k))goto end;
  size_t done=0;while(done<k){ssize_t z=write(out,buf+done,k-done);if(z<=0)goto end;done+=z;}at+=k;}
 if(fsync(out)||fstat(fileno(f),&b)||a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||a.st_size!=b.st_size||a.st_mtim.tv_sec!=b.st_mtim.tv_sec||a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec||a.st_ctim.tv_sec!=b.st_ctim.tv_sec||a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec)goto end;
 printf("FIRST_FRAME_JPEG_COPIED bytes=%u alignment_padding=%u\n",n,padding);rc=0;
end:if(rc)fprintf(stderr,"FIRST_FRAME_JPEG_FAILURE errno=%d w=%u h=%u off=%u count=%u output_owned=%d\n",errno,w,height,off,n,owned);if(fd>=0)close(fd);if(f)fclose(f);if(out>=0&&close(out))rc=1;fm_free(&d);if(rc&&owned)unlink(argv[2]);return rc;
}
