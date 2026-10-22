/* 自写私有实验：由已合成 DNG 生成真实 JPEG 预览和预览/RAW 双 IFD 容器。
 * 不连接服务、不修改源文件、不登记相册；不声称厂商完整 3FR 兼容。
 * 色彩是简单矩阵/WB/sRGB 预览，不是 HNCS；RAW 字节完整保留。
 * JPEG 接口头保留上游声明；运行时使用相机自带 libjpeg，不分发厂商库。 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "first_frame_metadata.h"
#include "readback_digest.h"
#include "jpeglib.h"
#include "factory_preview_extent.h"
#ifdef __ANDROID__
_Static_assert(sizeof(struct jpeg_compress_struct)==520,"fixed camera JPEG62 ABI");
#endif
static volatile sig_atomic_t cancelled;
static void stop(int sig){(void)sig;cancelled=1;}
typedef struct {struct jpeg_error_mgr pub;jmp_buf jump;} JError;
static void jpeg_error(j_common_ptr p){JError *e=(JError*)p->err;(*p->err->output_message)(p);longjmp(e->jump,1);}
static int num(FmIfd *d,unsigned id,uint32_t *n){FmTag*t=fm_find(d,id);if(!t||t->count!=1||(t->type!=3&&t->type!=4))return 0;*n=t->type==3?fm16(t->data):fm32(t->data);return 1;}
static int eq(FmIfd*d,unsigned id,uint32_t n){uint32_t v;return num(d,id,&v)&&v==n;}
static int sh(FmIfd*d,unsigned id,unsigned n){unsigned char p[2];if(n>65535)return 0;fmw16(p,(uint16_t)n);return fm_put(d,id,3,1,p);}
static int mat_inverse(const double a[9],double b[9]){
 double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
 if(!isfinite(det)||fabs(det)<1e-9)return 0;
 double c[]={a[4]*a[8]-a[5]*a[7],a[2]*a[7]-a[1]*a[8],a[1]*a[5]-a[2]*a[4],
 a[5]*a[6]-a[3]*a[8],a[0]*a[8]-a[2]*a[6],a[2]*a[3]-a[0]*a[5],
 a[3]*a[7]-a[4]*a[6],a[1]*a[6]-a[0]*a[7],a[0]*a[4]-a[1]*a[3]};
 for(unsigned i=0;i<9;i++){b[i]=c[i]/det;if(!isfinite(b[i])||fabs(b[i])>100)return 0;}return 1;
}
static int colors(FmIfd*d,double matrix[9],double wb[3]){
 FmTag*m=fm_find(d,50721),*n=fm_find(d,50728);if(!m||m->type!=10||m->count!=9||!n||n->type!=5||n->count!=3)return 0;
 const double xyz[]={.4124564,.3575761,.1804375,.2126729,.7151522,.0721750,.0193339,.1191920,.9503041};
 double cam[9],a[9]={0},neutral[3];
 for(unsigned i=0;i<9;i++){int32_t den=(int32_t)fm32(m->data+8*i+4);if(!den)return 0;cam[i]=(double)(int32_t)fm32(m->data+8*i)/den;if(!isfinite(cam[i])||fabs(cam[i])>100)return 0;}
 for(unsigned i=0;i<3;i++){
  uint32_t den=fm32(n->data+8*i+4);if(!den)return 0;neutral[i]=(double)fm32(n->data+8*i)/den;if(neutral[i]<=0||neutral[i]>100)return 0;
  for(unsigned j=0;j<3;j++)for(unsigned k=0;k<3;k++)a[3*i+j]+=cam[3*i+k]*xyz[3*k+j];
  double sum=a[3*i]+a[3*i+1]+a[3*i+2];if(fabs(sum)<1e-9)return 0;
  for(unsigned j=0;j<3;j++)a[3*i+j]/=sum;
 }
 for(unsigned i=0;i<3;i++){wb[i]=neutral[1]/neutral[i];if(wb[i]<.01||wb[i]>100)return 0;}
 return mat_inverse(a,matrix);
}
static unsigned char gamma_byte(double v){if(v<0)v=0;if(v>1)v=1;v=v<=.0031308?12.92*v:1.055*pow(v,1/2.4)-.055;return (unsigned char)floor(255*v+.5);}
static int preview(FILE *src,uint64_t length,uint32_t rawoff,unsigned w,unsigned h,FmIfd *d,FILE *jpeg,unsigned *pw,unsigned *ph){
 /* 固定 32 像素对齐，避免机身 NV12/JPEG 边缘 padding 被显示。
  * 每个采样盒边界均在完整 2x2 CFA 上，覆盖全图，不裁切源 RAW。 */
 const unsigned block=16;*pw=1920;*ph=1440;
 double matrix[9],wb[3];if(!colors(d,matrix,wb))return 0;
 unsigned char *rows=malloc((size_t)w*2*block),*rgb=malloc(*pw*3);if(!rows||!rgb){free(rows);free(rgb);return 0;}
 /* setjmp 后只依赖堆上的编码器，错误时不使用被改动的非 volatile 局部状态。 */
 struct jpeg_compress_struct *c=calloc(1,sizeof *c);JError e;volatile int ok=0;if(!c)goto done;
 c->err=jpeg_std_error(&e.pub);e.pub.error_exit=jpeg_error;
 if(setjmp(e.jump))goto destroy;
 jpeg_create_compress(c);jpeg_stdio_dest(c,jpeg);c->image_width=*pw;c->image_height=*ph;c->input_components=3;c->in_color_space=JCS_RGB;
 jpeg_set_defaults(c);jpeg_set_quality(c,90,TRUE);jpeg_start_compress(c,TRUE);
 for(unsigned py=0;py<*ph;py++){
  unsigned y=2*(unsigned)((uint64_t)py*(h/2)/ *ph);
  unsigned endy=2*(unsigned)((uint64_t)(py+1)*(h/2)/ *ph),ny=endy-y;
  if(!ny||ny>block)goto destroy;
  if(cancelled||!fm_read(src,length,rawoff+(uint64_t)y*w*2,rows,(size_t)ny*w*2))goto destroy;
  for(unsigned px=0;px<*pw;px++){
   unsigned x=2*(unsigned)((uint64_t)px*(w/2)/ *pw);
   unsigned endx=2*(unsigned)((uint64_t)(px+1)*(w/2)/ *pw),nx=endx-x;
   uint64_t sum[3]={0};unsigned count[3]={0};if(!nx||nx>block)goto destroy;
#ifdef PS_REFERENCE_PREVIEW
   for(unsigned dy=0;dy<ny;dy++)for(unsigned dx=0;dx<nx;dx++){
    unsigned channel=(dy&1)?((dx&1)?1:2):((dx&1)?0:1); /* GRBG */
    sum[channel]+=fm16(rows+2*((size_t)dy*w+x+dx));count[channel]++;
   }
#else
   /* 采样盒边界原本就是完整 CFA；直接累加 GRBG 四元组，不逐像素分支。
    * 整数和及各通道样本数与参考实现相同，色彩矩阵/gamma/编码参数不变。 */
   for(unsigned dy=0;dy<ny;dy+=2)for(unsigned dx=0;dx<nx;dx+=2){
    const unsigned char *q=rows+2*((size_t)dy*w+x+dx);
    sum[0]+=fm16(q+2);sum[1]+=fm16(q)+fm16(q+2*w+2);sum[2]+=fm16(q+2*w);
   }
   count[0]=count[2]=(nx/2)*(ny/2);count[1]=2*count[0];
#endif
   double camera[3];for(unsigned k=0;k<3;k++){if(!count[k])goto destroy;camera[k]=(double)sum[k]/count[k]/65535*wb[k];}
   for(unsigned k=0;k<3;k++)rgb[px*3+k]=gamma_byte(matrix[k*3]*camera[0]+matrix[k*3+1]*camera[1]+matrix[k*3+2]*camera[2]);
  }
  JSAMPROW row=rgb;if(jpeg_write_scanlines(c,&row,1)!=1)goto destroy;
 }
 jpeg_finish_compress(c);ok=!fflush(jpeg)&&!ferror(jpeg);
destroy:jpeg_destroy_compress(c);
done:free(c);free(rows);free(rgb);return ok;
}
static int stream(FILE*in,FILE*out,uint64_t bytes,PsReadbackDigest *hash){unsigned char b[65536];while(bytes){size_t n=bytes<sizeof b?(size_t)bytes:sizeof b;if(cancelled||fread(b,1,n,in)!=n||fwrite(b,1,n,out)!=n)return 0;ps_digest_update(hash,b,n);bytes-=n;}return 1;}
static unsigned legacy_black;
static const char *legacy_first;
static int legacy_range_ok(FmIfd *raw){
 uint32_t black,white;
 if(!num(raw,50714,&black)||!num(raw,50717,&white)||black>8192||white>65535||white<=black)return 0;
 legacy_black=black;return 1;
}
static int legacy_factory_preview(FILE*out,unsigned*w,unsigned*h){
 FILE*f=fopen(legacy_first,"rb");struct stat st;unsigned char hdr[8];FmIfd root={0};
 fprintf(stderr,"FACTORY_PREVIEW_SOURCE path=%s opened=%d\n",legacy_first,f!=NULL);
 unsigned ids[]={256,257,259,273,279};uint32_t off,bytes;int ok=0;
 if(!f||fstat(fileno(f),&st)||!fm_read(f,st.st_size,0,hdr,8)||memcmp(hdr,"II*\0",4)||
 !fm_parse(f,st.st_size,fm32(hdr+4),&root,ids,5)||!eq(&root,259,7)||
 !num(&root,256,w)||!num(&root,257,h)||*w!=3888||*h!=2918||
 !num(&root,273,&off)||!num(&root,279,&bytes)||bytes>8*1024*1024||bytes<4||(uint64_t)off+bytes>(uint64_t)st.st_size)goto end;
 fprintf(stderr,"FACTORY_PREVIEW_LAYOUT width=%u height=%u offset=%u bytes=%u\n",*w,*h,off,bytes);
 unsigned char*data=malloc(bytes);if(!data)goto end;
 if(fm_read(f,st.st_size,off,data,bytes)){
  bytes=(unsigned)factory_preview_extent(data,bytes);
  if(!bytes){free(data);goto end;}
  fprintf(stderr,"FACTORY_PREVIEW_MARKERS bytes=%u head=%02x%02x tail=%02x%02x\n",bytes,data[0],data[1],data[bytes-2],data[bytes-1]);
  ok=bytes>4&&data[0]==255&&data[1]==216&&data[bytes-2]==255&&data[bytes-1]==217&&fwrite(data,1,bytes,out)==bytes;
 }
 free(data);
end:if(f)fclose(f);fm_free(&root);return ok;
}
static int build(const char *source,const char *target){
 const char *phase="paths";
 int rc=1,owned=0,dir=-1;FILE *in=NULL,*jpg=NULL,*out=NULL;FmIfd raw={0},root={0},exif={0};unsigned char h8[8],*header=NULL;
 struct stat original,after;char part[4096],parent[4096];uint32_t w,h,rawoff,rawbytes,exifoff,orientation;unsigned pw=0,ph=0;
 const unsigned ids[]={256,257,258,259,262,273,274,277,278,279,284,33421,33422,34665,50706,50707,50708,50710,50711,50713,50714,50717,50719,50720,50721,50728,50778,271,272,306,315,33432};
 const unsigned exids[]={33434,33437,34850,34855,34864,34865,34866,34867,36864,36867,36868,36880,36881,36882,37377,37378,37379,37380,37381,37382,37383,37384,37385,37386,37520,37521,37522,40960,40962,40963,41486,41487,41488,41986,41987,41989,42032,42033,42034,42035,42036,42037};
 if(source[0]!='/'||target[0]!='/'||strlen(target)>4000||strstr(target,"/../"))goto done;
 if(!lstat(target,&after)||errno!=ENOENT)goto done;
 snprintf(part,sizeof part,"%s.partial",target);strcpy(parent,target);char *slash=strrchr(parent,'/');if(!slash||slash==parent)goto done;*slash=0;
 dir=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)goto done;
 phase="input-open";
 int fd=open(source,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)goto done;in=fdopen(fd,"rb");if(!in){close(fd);goto done;}
 if(fstat(fileno(in),&original)||!S_ISREG(original.st_mode)||original.st_size<8||original.st_size>UINT32_MAX||
  !fm_read(in,original.st_size,0,h8,8)||memcmp(h8,"II*\0",4)||!fm_parse(in,original.st_size,fm32(h8+4),&raw,ids,sizeof ids/sizeof *ids))goto done;
 phase="input-layout";
 if(!num(&raw,256,&w)||!num(&raw,257,&h)||w!=23310||h!=17482||!num(&raw,273,&rawoff)||rawoff>FM_LIMIT||
  !num(&raw,279,&rawbytes)||rawbytes!=(uint64_t)w*h*2||(uint64_t)rawoff+rawbytes!=(uint64_t)original.st_size||
  !eq(&raw,258,16)||!eq(&raw,259,1)||!eq(&raw,262,32803)||!eq(&raw,277,1)||!eq(&raw,278,h)||!eq(&raw,284,1)||
  !legacy_range_ok(&raw)||!num(&raw,274,&orientation)||orientation<1||orientation>8||
  !num(&raw,34665,&exifoff)||!fm_parse(in,original.st_size,exifoff,&exif,exids,sizeof exids/sizeof *exids))goto done;
 FmTag *cfa=fm_find(&raw,33422);if(!cfa||cfa->type!=1||cfa->count!=4||memcmp(cfa->data,"\1\0\2\1",4))goto done;
 phase="preview-open";
 char scratchpath[4096];snprintf(scratchpath,sizeof scratchpath,"%s.preview-work",target);
 jpg=fopen(scratchpath,"wbx+");if(!jpg)goto done;
 /* 只 unlink 刚刚独占创建的空工作文件，打开的句柄用于 JPEG 临时数据。 */
 phase="factory-preview";
 if(unlink(scratchpath)||!legacy_factory_preview(jpg,&pw,&ph))goto done;
 off_t jpegbytes=ftello(jpg);if(jpegbytes<=0||jpegbytes>8*1024*1024)goto done;
 /* 根目录必须保留 DNG 格式声明。删除它们虽让 Windows 属性转而显示
  * RAW 尺寸，却使 Adobe 拒绝本实验容器。优先完整 RAW 解码兼容性；
  * Windows 可能仍显示预览尺寸，不能为修正属性再破坏格式声明。
  * 不复制未知 MakerNote/身份，不声称原厂 3FR 或 Phocus 传输兼容。 */
 phase="metadata";
 FmTag *dv=fm_find(&raw,50706),*bv=fm_find(&raw,50707);
 if(!dv||!bv||dv->type!=1||bv->type!=1||dv->count!=4||bv->count!=4||
    memcmp(dv->data,"\1\4\0\0",4)||memcmp(bv->data,"\1\1\0\0",4))goto done;
 unsigned copyroot[]={271,272,274,306,315,33432,50706,50707,50708,50721,50728,50778};
 for(unsigned i=0;i<sizeof copyroot/sizeof *copyroot;i++)if(!fm_copy(&root,&raw,copyroot[i]))goto done;
 if(!sh(&root,256,pw)||!sh(&root,257,ph)||!sh(&root,259,7)||!sh(&root,262,6)||!sh(&root,277,3)||!sh(&root,284,1)||
  !fm_number(&root,278,ph)||!fm_number(&root,273,0)||!fm_number(&root,279,(uint32_t)jpegbytes)||!fm_number(&root,330,0)||!fm_number(&root,34665,0)||
  !fm_number(&root,254,1)||!fm_string(&root,305,"X2DII experimental merge + matrix preview")||
  !fm_string(&root,270,"Experimental 407 MP RAW with first-frame factory overview; detail render separate"))goto done;
 unsigned char bits[]={8,0,8,0,8,0};if(!fm_put(&root,258,3,3,bits))goto done;
 /* 原数据结构移入 SubIFD；只在根保存重建后的 EXIF 指针，删除旧指针。 */
 FmTag *oldptr=fm_find(&raw,34665);if(!oldptr)goto done;
 unsigned idx=(unsigned)(oldptr-raw.tags);raw.total-=oldptr->size;free(oldptr->data);memmove(raw.tags+idx,raw.tags+idx+1,(raw.count-idx-1)*sizeof(FmTag));raw.count--;
 if(!sh(&raw,256,w)||!sh(&raw,257,h)||!fm_number(&raw,254,0))goto done;
 /* 原厂 fullsizeInfo 固定传感器尺寸，不能只补 crop 便宣称 RAW 放大可用。
  * 机身边框复测构建仅改预览，不在该路径启用新的裁切解析。 */
#ifndef PS_PREVIEW_ALIGNMENT_ONLY
 /* 原厂 cropRectangle 明确读取 vector<unsigned short>，LONG 向量不可替代。 */
 unsigned char origin[4]={0},crop[4],black[8]={0};fmw16(crop,w);fmw16(crop+2,h);fmw32(black,legacy_black);fmw32(black+4,1);
 if(!fm_put(&raw,50719,3,2,origin)||!fm_put(&raw,50720,3,2,crop)||!fm_put(&raw,50714,5,1,black))goto done;
#endif
 unsigned ro=8+2+12*root.count+4,eo=ro+2+12*raw.count+4,cursor=eo+2+12*exif.count+4;
 unsigned hs=(cursor+fm_extra(&root)+fm_extra(&raw)+fm_extra(&exif)+15)&~15u;
 unsigned pad=(16-(unsigned)jpegbytes%16)%16;
 if(hs>FM_LIMIT||!fm_number(&root,330,ro)||!fm_number(&root,34665,eo)||!fm_number(&root,273,hs)||!fm_number(&raw,273,hs+(uint32_t)jpegbytes+pad))goto done;
 header=calloc(1,hs);if(!header)goto done;memcpy(header,"II*\0",4);fmw32(header+4,8);
 fm_encode(header,8,&root,&cursor);fm_encode(header,ro,&raw,&cursor);fm_encode(header,eo,&exif,&cursor);if(cursor>hs)goto done;
 phase="write";
 out=fopen(part,"wbx");if(!out)goto done;owned=1;PsReadbackDigest hash;ps_digest_init(&hash);
 if(fwrite(header,1,hs,out)!=hs)goto done;
 ps_digest_update(&hash,header,hs);
 if(fseeko(jpg,0,SEEK_SET)||!stream(jpg,out,(uint64_t)jpegbytes,&hash))goto done;
 unsigned char padding[16]={0};if(fwrite(padding,1,pad,out)!=pad)goto done;ps_digest_update(&hash,padding,pad);
 if(fseeko(in,rawoff,SEEK_SET)||!stream(in,out,rawbytes,&hash))goto done;
 if(fstat(fileno(in),&after)||original.st_ino!=after.st_ino||original.st_size!=after.st_size||original.st_mtim.tv_sec!=after.st_mtim.tv_sec||original.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)goto done;
 if(fflush(out)||fsync(fileno(out)))goto done;
 {int err=fclose(out);out=NULL;if(err)goto done;}
 phase="readback";
 out=fopen(part,"rb");if(!out)goto done;uint64_t gotbytes=0;PsReadbackDigest readhash;ps_digest_init(&readhash);unsigned char block[65536];size_t got;
 while((got=fread(block,1,sizeof block,out))){if(cancelled)goto done;gotbytes+=got;ps_digest_update(&readhash,block,got);}
 if(ferror(out)||gotbytes!=(uint64_t)hs+jpegbytes+pad+rawbytes||!ps_digest_equal(&hash,&readhash))goto done;
 fclose(out);out=NULL;
#ifdef PS_HOST_VALIDATION_ONLY
 /* WSL /mnt/c 不支持 RENAME_NOREPLACE；主机只验内容并显式保留 partial。
  * 此构建不能部署相机，不能作为原子发布已通过的证明。 */
 owned=0;puts("HOST_VALIDATED_PARTIAL_NOT_PUBLISHED");rc=0;goto done;
#endif
 phase="publish-private";
 if(cancelled||syscall(SYS_renameat2,AT_FDCWD,part,AT_FDCWD,target,1))goto done;
 owned=0;
 if(fsync(dir)){fputs("PUBLISHED_DURABILITY_UNCONFIRMED\n",stderr);goto done;}
 printf("PREVIEW_CONTAINER_READBACK_PASS raw=%ux%u preview=%ux%u jpeg_bytes=%lld total=%llu\n",w,h,pw,ph,(long long)jpegbytes,(unsigned long long)gotbytes);rc=0;
done:
 if(rc)fprintf(stderr,"PREVIEW_CONTAINER_FAILED phase=%s errno=%d cancelled=%d\n",phase,errno,cancelled);
 if(out)fclose(out);
 if(jpg)fclose(jpg);
 if(in)fclose(in);
 if(dir>=0)close(dir);
 if(owned)unlink(part);
 free(header);fm_free(&raw);fm_free(&root);fm_free(&exif);return rc;
}
#ifndef PS_ALBUM_LIBRARY
int main(int argc,char**argv){if(argc!=4)return 2;legacy_first=argv[2];signal(SIGTERM,stop);signal(SIGINT,stop);return build(argv[1],argv[3]);}
#endif
