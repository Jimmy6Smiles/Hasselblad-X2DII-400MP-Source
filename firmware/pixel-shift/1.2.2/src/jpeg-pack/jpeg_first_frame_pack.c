/* JPEG trial packer: first-frame EXIF, unchanged JPEG scan data, 4K padding.
 * No copied RAW offsets or opaque MakerNote. Never overwrites any input. */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "first_frame_metadata.h"
static int same(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static FILE*input(const char*p,struct stat*s){int fd=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return NULL;if(fstat(fd,s)||!S_ISREG(s->st_mode)){close(fd);return NULL;}FILE*f=fdopen(fd,"rb");if(!f)close(fd);return f;}
int main(int argc,char**argv){
 if(argc!=4&&argc!=5)return 2;
 int rc=1,owned=0;FILE*raw=NULL,*jpg=NULL,*out=NULL,*preview=NULL;struct stat rs,js,now,ps;
 FmIfd root={0},exif={0},maker={0};unsigned char h[8],*tiff=NULL;
 raw=input(argv[1],&rs);jpg=input(argv[2],&js);if(!raw||!jpg||rs.st_size<100000000||js.st_size<4)goto end;
 /* This packer accepts our encoder's standalone baseline JPEG, not arbitrary containers. */
 if(fgetc(jpg)!=255||fgetc(jpg)!=216)goto end;
 int found=0,sos=0;
 while(ftello(jpg)<2097152){
  if(fgetc(jpg)!=255)goto end;int marker=fgetc(jpg),a=fgetc(jpg),b=fgetc(jpg);if(marker<0||a<0||b<0)goto end;
  unsigned n=((unsigned)a<<8)|b;if(n<2)goto end;
  if(marker==0xe1||marker==0xe2)goto end;
  if(marker==0xc0){unsigned char sof[6];if(found||n<8||fread(sof,1,6,jpg)!=6||sof[0]!=8||((sof[1]<<8)|sof[2])!=17482||((sof[3]<<8)|sof[4])!=23310||sof[5]!=3)goto end;found=1;if(fseeko(jpg,n-8,SEEK_CUR))goto end;}
  else if(marker==0xda){sos=1;break;}
  else if(marker==0xd8||marker==0xd9||marker==0||fseeko(jpg,n-2,SEEK_CUR))goto end;
 }
 if(!found||!sos||fseeko(jpg,-2,SEEK_END)||fgetc(jpg)!=255||fgetc(jpg)!=217)goto end;
 if(argc==5){
  preview=input(argv[4],&ps);if(!preview||ps.st_size<4||ps.st_size>16000000||fgetc(preview)!=255||fgetc(preview)!=216)goto end;
  int valid=0;for(unsigned i=0;i<128;i++){
   if(fgetc(preview)!=255)goto end;int m=fgetc(preview),a=fgetc(preview),b=fgetc(preview);if(a<0||b<0)goto end;
   unsigned z=((unsigned)a<<8)|b;if(z<2)goto end;
   if(m==192){unsigned char sof[6];if(z<8||fread(sof,1,6,preview)!=6||sof[0]!=8||((sof[1]<<8)|sof[2])!=2918||((sof[3]<<8)|sof[4])!=3888||sof[5]!=3)goto end;valid=1;break;}
   if(m==218||fseeko(preview,z-2,SEEK_CUR))goto end;
  }
  if(!valid||fseeko(preview,-2,SEEK_END)||fgetc(preview)!=255||fgetc(preview)!=217)goto end;
 }
 if(!fm_read(raw,rs.st_size,0,h,8)||memcmp(h,"II*\0",4))goto end;
 const unsigned roots[]={271,272,274,282,283,296,305,306,315,33432,34665,50735};
 const unsigned tags[]={18246,18249,33434,33437,34850,34855,34864,34865,34866,34867,36864,36867,36868,36880,36881,36882,37377,37378,37379,37380,37381,37382,37383,37384,37385,37386,37500,37520,37521,37522,40960,41486,41487,41488,41986,41987,41989,42016,42032,42033,42034,42035,42036,42037};
 if(!fm_parse(raw,rs.st_size,fm32(h+4),&root,roots,sizeof roots/sizeof *roots))goto end;
 FmTag*p=fm_find(&root,34665);if(!p||p->type!=4||p->count!=1)goto end;
 if(!fm_parse(raw,rs.st_size,fm32(p->data),&exif,tags,sizeof tags/sizeof *tags))goto end;
 /* Factory JPEG has a TIFF-relative MakerNote IFD. Copy only typed capture
  * values observed in its schema, never RAW offsets, multishot identity,
  * opaque processing/calibration blobs, or another photo's parameter values. */
 FmTag*mn=fm_find(&exif,37500);if(!mn||mn->type!=7||mn->size<6)goto end;
 unsigned maker_ids[]={5,7,8,17,18,20,21,22,23,24,69,70,71,74,91,92,93,94,95,96,97,99,112,113,118,119,120,122,123,124,125,126,127,128,129,130,131,50728};
 /* Locate the source IFD rather than interpreting its copied offset bytes. */
 unsigned char countbuf[2],entry[12];uint32_t src_exif=fm32(p->data),maker_off=0;
 if(!fm_read(raw,rs.st_size,src_exif,countbuf,2))goto end;
 for(unsigned i=0;i<fm16(countbuf);i++){
  if(!fm_read(raw,rs.st_size,(uint64_t)src_exif+2+12*i,entry,12))goto end;
  if(fm16(entry)==37500){maker_off=fm32(entry+8);break;}
 }
 if(!maker_off||!fm_parse(raw,rs.st_size,maker_off,&maker,maker_ids,sizeof maker_ids/sizeof *maker_ids))goto end;
 /* 1.3.16.2 MetadataTopImage uses tag 23 IMPRINT_DYN_INFO and tag 24
  * IMPRINT_LENS_INFO for playback TV/focal length. These are version byte
  * plus 16 inline data bytes, not TIFF offsets or calibration tables. */
 for(unsigned i=0;i<maker.count;i++){
  FmTag*t=&maker.tags[i];
  if(t->id==23||t->id==24){
   if(t->type!=7||t->count!=17||t->size!=17||t->data[0]!=1)goto end;
  }else if(t->type==7||t->size>64)goto end;
 }
 if(!fm_find(&maker,5))goto end;
 unsigned maker_size=6+12*maker.count+fm_extra(&maker);
 unsigned char*empty=calloc(1,maker_size);if(!empty)goto end;
 int maker_ok=fm_put(&exif,37500,7,maker_size,empty);free(empty);if(!maker_ok)goto end;
 if(!fm_find(&exif,33434)||!fm_aperture_valid(&exif)||!fm_find(&exif,34855))goto end;
 if(!fm_number(&exif,40962,23310)||!fm_number(&exif,40963,17482))goto end;
 /* No invented ICC profile or claimed color-space calibration. */
 unsigned char unknown[2]={255,255};if(!fm_put(&exif,40961,3,1,unknown))goto end;
 unsigned eo=8+6+12*root.count,cursor=eo+6+12*exif.count;
 unsigned total=cursor+fm_extra(&root)+fm_extra(&exif);
 if(total>65527||!fm_number(&root,34665,eo))goto end;
 tiff=calloc(1,total);if(!tiff)goto end;memcpy(tiff,"II*\0",4);fmw32(tiff+4,8);
 fm_encode(tiff,8,&root,&cursor);fm_encode(tiff,eo,&exif,&cursor);if(cursor!=total)goto end;
 unsigned dest_maker=0;
 for(unsigned i=0;i<exif.count;i++){unsigned char*e=tiff+eo+2+12*i;if(fm16(e)==37500){dest_maker=fm32(e+8);break;}}
 if(!dest_maker||dest_maker+maker_size>total)goto end;
 unsigned maker_cursor=dest_maker+6+12*maker.count;fm_encode(tiff,dest_maker,&maker,&maker_cursor);
 if(maker_cursor!=dest_maker+maker_size)goto end;
 int fd=open(argv[3],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)goto end;owned=1;out=fdopen(fd,"wb");if(!out){close(fd);goto end;}
 unsigned n=total+8;unsigned char prefix[]={255,216,255,225,(unsigned char)(n>>8),(unsigned char)n,'E','x','i','f',0,0};
 if(fwrite(prefix,1,sizeof prefix,out)!=sizeof prefix||fwrite(tiff,1,total,out)!=total||fseeko(jpg,0,SEEK_SET))goto end;
 /* Match factory JPEG layout: COM padding ends with the encoder's SOI,
  * at an absolute 4K boundary. Merely padding EOF/preview is insufficient.
  * The outer decoder consumes that SOI as the final two COM payload bytes. */
 uint64_t header_end=12u+total+(preview?90u:0u);
 uint64_t main_start=(header_end+4u+4095u)&~UINT64_C(4095);
 uint64_t main_size=0,preview_start=0;
 if(preview){
  unsigned char mp[90]={255,226,0,88,'M','P','F',0,'I','I',42,0};
  fmw32(mp+12,8);fmw16(mp+16,3);
  fmw16(mp+18,0xb000);fmw16(mp+20,7);fmw32(mp+22,4);memcpy(mp+26,"0100",4);
  fmw16(mp+30,0xb001);fmw16(mp+32,4);fmw32(mp+34,1);fmw32(mp+38,2);
  fmw16(mp+42,0xb002);fmw16(mp+44,7);fmw32(mp+46,32);fmw32(mp+50,50);
  main_size=main_start+(uint64_t)js.st_size;preview_start=(main_size+4095)&~UINT64_C(4095);
  if(main_size>UINT32_MAX||preview_start>UINT32_MAX)goto end;
  fmw32(mp+58,0x30000);fmw32(mp+62,(uint32_t)main_size);
  fmw32(mp+74,0x10003);fmw32(mp+78,(uint32_t)ps.st_size);fmw32(mp+82,(uint32_t)(preview_start-(12u+total+8u)));
  if(fwrite(mp,1,sizeof mp,out)!=sizeof mp)goto end;
 }
 unsigned char buf[65536];unsigned com=(unsigned)(main_start-header_end);
 if(com<4||com>65535||(uint64_t)ftello(out)!=header_end)goto end;
 unsigned char padding_header[]={255,254,(unsigned char)(com>>8),(unsigned char)com};
 memset(buf,0,com-4);
 if(fwrite(padding_header,1,4,out)!=4||fwrite(buf,1,com-4,out)!=com-4)goto end;
 size_t got;while((got=fread(buf,1,sizeof buf,jpg)))if(fwrite(buf,1,got,out)!=got)goto end;
 if(ferror(jpg))goto end;
 if(preview){
  if((uint64_t)ftello(out)!=main_size)goto end;unsigned pad=(unsigned)(preview_start-main_size);memset(buf,0,pad);
  if(fwrite(buf,1,pad,out)!=pad||fseeko(preview,0,SEEK_SET))goto end;
  while((got=fread(buf,1,sizeof buf,preview)))if(fwrite(buf,1,got,out)!=got)goto end;
  if(ferror(preview)||fstat(fileno(preview),&now)||!same(&ps,&now))goto end;
 }
 off_t size=ftello(out);if(size<0)goto end;unsigned pad=(4096-size%4096)%4096;memset(buf,0,pad);if(fwrite(buf,1,pad,out)!=pad||fflush(out)||fsync(fileno(out)))goto end;
 if(fstat(fileno(raw),&now)||!same(&rs,&now)||fstat(fileno(jpg),&now)||!same(&js,&now))goto end;
 if(fclose(out)){out=NULL;goto end;}out=NULL;rc=0;puts("JPEG_FIRST_FRAME_EXIF_4K_PACK_OK");
end:
 if(out)fclose(out);if(raw)fclose(raw);if(jpg)fclose(jpg);if(preview)fclose(preview);free(tiff);fm_free(&root);fm_free(&exif);fm_free(&maker);
 if(rc&&owned)unlink(argv[3]);return rc;
}
