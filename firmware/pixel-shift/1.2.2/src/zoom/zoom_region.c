/* 自写私有适配：对已验证的合成 GRBG RAW 读取 1:1 区域。
 * 不调用厂商 RAW 解码器，不读取其他进程，不连接相机或修改源文件。
 * 输出 PPM 是待接入图像提供器的原型，不是已安装的相册放大功能。
 * 色彩沿用实验预览矩阵；双线性去马赛克，不宣称 HNCS。 */
#define PS_ALBUM_LIBRARY
#include "album_candidate.c"

#define ZOOM_TILE_MAX 1024u
static int display_range(FmIfd *d,double *black,double *white){
 FmTag *t=fm_find(d,50714);uint32_t v;
 if(!t||t->count!=1)return 0;
 if(t->type==5){if(t->size!=8||!fm32(t->data+4))return 0;*black=(double)fm32(t->data)/fm32(t->data+4);}
 else {if(!num(d,50714,&v))return 0;*black=v;}
 if(!num(d,50717,&v))return 0;*white=v;
 return isfinite(*black)&&*black>=0&&*black<=8192&&*white<=65535&&*white>*black;
}
static unsigned channel(unsigned x,unsigned y){return (y&1)?((x&1)?1:2):((x&1)?0:1);}
static int integer(const char *s,unsigned *v){
 if(!*s)return 0;
 uint64_t n=0;
 while(*s){if(*s<'0'||*s>'9')return 0;n=n*10+(*s++-'0');if(n>65535)return 0;}
 *v=(unsigned)n;return 1;
}
static int same_file(const struct stat *a,const struct stat *b){
 return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int region_checked(const char *source,const char *target,unsigned x,unsigned y,unsigned tw,unsigned th,const struct stat *expected){
 int rc=1,owned=0;FILE *in=NULL,*out=NULL;FmIfd root={0},raw={0};
 unsigned char hdr[8],*rows=NULL,*rgb=NULL;char part[4096];struct stat before,after;
 uint32_t off=0,bytes=0,w=0,h=0,sub=0,stride=0;int padded=0;double matrix[9],wb[3],black=0,white=0;
 const unsigned ids[]={256,257,258,259,262,273,274,277,278,279,284,330,33421,33422,50713,50714,50717,50719,50720,50721,50728};
 if(!tw||!th||tw>23310||th>17482||source[0]!='/'||target[0]!='/'||strlen(target)>4000||strstr(target,"/../"))goto done;
 if(!lstat(target,&after)||errno!=ENOENT)goto done;
 snprintf(part,sizeof part,"%s.partial",target);
 int fd=open(source,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)goto done;
 in=fdopen(fd,"rb");if(!in){close(fd);goto done;}
 if(fstat(fileno(in),&before)||!S_ISREG(before.st_mode)||before.st_size<8||before.st_size>UINT32_MAX||
    !fm_read(in,before.st_size,0,hdr,8)||memcmp(hdr,"II*\0",4))goto done;
 if(expected&&!same_file(&before,expected))goto done;
 uint32_t ro=fm32(hdr+4);if(ro>=FM_LIMIT||!fm_parse(in,before.st_size,ro,&root,ids,sizeof ids/sizeof *ids))goto done;
 if(fm_find(&root,330)){
  if(!num(&root,330,&sub)||sub==ro||sub>=FM_LIMIT||!fm_parse(in,before.st_size,sub,&raw,ids,sizeof ids/sizeof *ids))goto done;
 }else{raw=root;memset(&root,0,sizeof root);}
 if(!num(&raw,256,&w)||!num(&raw,257,&h)||(w!=23310&&w!=23312)||h!=17482||
    !num(&raw,273,&off)||off>8*1024*1024||!num(&raw,279,&bytes)||bytes!=(uint64_t)w*h*2||
    (uint64_t)off+bytes>(uint64_t)before.st_size||!eq(&raw,258,16)||!eq(&raw,259,1)||!eq(&raw,262,32803)||
    !eq(&raw,277,1)||!eq(&raw,278,h)||!eq(&raw,284,1)||!display_range(&raw,&black,&white)||
    !colors(&raw,matrix,wb))goto done;
 FmTag *cfa=fm_find(&raw,33422),*repeat=fm_find(&raw,33421);
 padded=w==23312;stride=w;
 if(padded){
  FmTag *o=fm_find(&raw,50719),*c=fm_find(&raw,50720);uint32_t jo,jb;
  if(!o||!c||o->type!=3||c->type!=3||o->count!=2||c->count!=2||fm16(o->data)!=1||fm16(o->data+2)||
     fm16(c->data)!=23310||fm16(c->data+2)!=17482||!num(&root,273,&jo)||!num(&root,279,&jb)||
     jo!=(uint64_t)off+bytes)goto done;
  uint64_t end=(uint64_t)jo+jb,padded_end=(uint64_t)jo+((uint64_t)jb+4095)/4096*4096;
  if((uint64_t)before.st_size!=end&&(uint64_t)before.st_size!=padded_end)goto done;
  w=23310;off+=2;
 }else if((uint64_t)off+bytes!=(uint64_t)before.st_size)goto done;
 if(x>=w||y>=h||tw>w-x||th>h-y)goto done;
 if(!cfa||cfa->type!=1||cfa->count!=4||memcmp(cfa->data,padded?"\0\1\1\2":"\1\0\2\1",4)||
    !repeat||repeat->type!=3||repeat->count!=2||fm16(repeat->data)!=2||fm16(repeat->data+2)!=2)goto done;
 // Source span can cover any zoom level; bound the decoded screen image.
 unsigned divisor=(tw>th?tw:th)+ZOOM_TILE_MAX-1;divisor/=ZOOM_TILE_MAX;
 unsigned ow=(tw+divisor-1)/divisor,oh=(th+divisor-1)/divisor;
 // Preserve the original 1:1 demosaic and color transform at sampled coordinates.
 unsigned left=x?x-1:0,right=x+tw<w?x+tw:w-1,rw=right-left+1;
 rows=malloc((size_t)rw*2*3);rgb=malloc((size_t)ow*3);if(!rows||!rgb)goto done;
 out=fopen(part,"wbx");if(!out)goto done;owned=1;
 if(fprintf(out,"P6\n%u %u\n255\n",ow,oh)<0)goto done;
 for(unsigned iy=0;iy<oh;iy++){
  unsigned yy=y+(unsigned)(((uint64_t)(2*iy+1)*th)/(2*oh));
  if(cancelled)goto done;
  for(int dy=-1;dy<=1;dy++){
   int sy=(int)yy+dy;
   if(sy>=0&&sy<(int)h&&!fm_read(in,before.st_size,off+((uint64_t)sy*stride+left)*2,rows+(dy+1)*rw*2,rw*2))goto done;
  }
  for(unsigned ix=0;ix<ow;ix++){
   unsigned xx=x+(unsigned)(((uint64_t)(2*ix+1)*tw)/(2*ow)),own=channel(xx,yy);uint32_t sums[3]={0};unsigned counts[3]={0};
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    int sx=(int)xx+dx,sy=(int)yy+dy;
    if(sx<0||sy<0||sx>=(int)w||sy>=(int)h)continue;
    unsigned k=channel((unsigned)sx,(unsigned)sy);
    // 对已有颜色保留中心采样，缺失颜色取相邻同色采样均值。
    if(k==own&&(dx||dy))continue;
    sums[k]+=fm16(rows+((dy+1)*rw+(unsigned)sx-left)*2);counts[k]++;
   }
   double cam[3];for(unsigned k=0;k<3;k++){if(!counts[k])goto done;cam[k]=fmax(0.0,(double)sums[k]/counts[k]-black)/(white-black)*wb[k];}
   for(unsigned k=0;k<3;k++)rgb[ix*3+k]=gamma_byte(matrix[k*3]*cam[0]+matrix[k*3+1]*cam[1]+matrix[k*3+2]*cam[2]);
  }
  if(fwrite(rgb,3,ow,out)!=ow)goto done;
 }
 if(fstat(fileno(in),&after)||!same_file(&before,&after)||fflush(out)||fsync(fileno(out)))goto done;
 {int e=fclose(out);out=NULL;if(e)goto done;}
 // link 是不可覆盖的发布；只删除本进程创建的 partial。
 if(cancelled||link(part,target))goto done;
 if(unlink(part))goto done;
 owned=0;rc=0;
 printf("REGION_CONTENT_READY x=%u y=%u width=%u height=%u rgb_bytes=%u working_pixel_bytes=%u\n",x,y,tw,th,tw*th*3,rw*6+tw*3);
done:
 if(out)fclose(out);
 if(in)fclose(in);
 if(owned)unlink(part);
 free(rows);free(rgb);fm_free(&root);fm_free(&raw);
 if(rc)fprintf(stderr,"REGION_FAILED errno=%d cancelled=%d\n",errno,cancelled);
 return rc;
}
static int region(const char *source,const char *target,unsigned x,unsigned y,unsigned tw,unsigned th){
 return region_checked(source,target,x,y,tw,th,NULL);
}
int main(int argc,char **argv){
 unsigned x,y,w,h;if(argc!=7||!integer(argv[3],&x)||!integer(argv[4],&y)||!integer(argv[5],&w)||!integer(argv[6],&h))return 2;
 signal(SIGTERM,stop);signal(SIGINT,stop);return region(argv[1],argv[2],x,y,w,h);
}
