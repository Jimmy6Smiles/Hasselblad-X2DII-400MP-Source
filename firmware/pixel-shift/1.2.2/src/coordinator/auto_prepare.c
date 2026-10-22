/* 自写私有适配：从六份完整原片构建任务，不拍摄、不发布、不删除照片。
 * 只接受已研究的 X2D II 1.3.16.2 未压缩布局；不宣称通用 3FR 解码器。 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include "first_frame_metadata.h"
#ifndef PS_PREPARE_CANCELLED
#ifdef PS_PREPARE_CANCEL_HOOK
extern int ps_prepare_cancelled(void);
#define PS_PREPARE_CANCELLED() ps_prepare_cancelled()
#else
#define PS_PREPARE_CANCELLED() 0
#endif
#endif

typedef struct { uint32_t tvn,tvd,avn,avd,iso,orientation,aperture_known; } Exposure;
static int exposure_equal(const Exposure *a,const Exposure *b){
    return a->tvn==b->tvn&&a->tvd==b->tvd&&a->iso==b->iso&&a->orientation==b->orientation&&
        a->aperture_known==b->aperture_known&&
        (!a->aperture_known||(a->avn==b->avn&&a->avd==b->avd));
}
typedef struct { uint64_t offset,size; uint32_t black[4],white; Exposure exposure; } Input;
static uint32_t gcd(uint32_t a,uint32_t b){while(b){uint32_t c=a%b;a=b;b=c;}return a;}
static int integer(FmIfd *d,unsigned id,uint32_t *out,unsigned count){
    FmTag *t=fm_find(d,id);if(!t||t->count!=count||(t->type!=3&&t->type!=4))return 0;
    for(unsigned i=0;i<count;i++)out[i]=t->type==3?fm16(t->data+2*i):fm32(t->data+4*i);
    return 1;
}
static int number(FmIfd *d,unsigned id,uint32_t expected){uint32_t v;return integer(d,id,&v,1)&&v==expected;}
static int text_is(FmIfd *d,unsigned id,const char *s){FmTag *t=fm_find(d,id);return t&&t->type==2&&t->size==strlen(s)+1&&!memcmp(t->data,s,t->size);}
static int rational(FmIfd *d,unsigned id,uint32_t *n,uint32_t *den){
    FmTag *t=fm_find(d,id);if(!t||t->type!=5||t->count!=1)return 0;
    *n=fm32(t->data);*den=fm32(t->data+4);if(!*n||!*den)return 0;
    uint32_t div=gcd(*n,*den);*n/=div;*den/=div;return 1;
}
static int parse(FILE *f,Input *v,FmIfd *root){
    static const unsigned roots[]={274,305,330,34665,50708,50721,50728,50778};
    static const unsigned raws[]={256,257,258,259,262,273,277,278,279,284,50714,50717,50719,50720,50830};
    static const unsigned exifs[]={33434,33437,34855};
    struct stat st;unsigned char h[8];uint32_t ptr,crop[2],length;
    FmIfd raw={0},exif={0};int ok=0;
    if(fstat(fileno(f),&st)||!S_ISREG(st.st_mode)||st.st_size<8||st.st_size>UINT32_MAX)goto done;
    v->size=(uint64_t)st.st_size;
    if(!fm_read(f,v->size,0,h,8)||memcmp(h,"II*\0",4)||
       !fm_parse(f,v->size,fm32(h+4),root,roots,sizeof roots/sizeof *roots)||
       !text_is(root,50708,"Hasselblad X2D II 100C")||!text_is(root,305,"1.3.16.2")||
       !integer(root,274,&v->exposure.orientation,1)||v->exposure.orientation<1||v->exposure.orientation>8||
       !integer(root,330,&ptr,1)||ptr==fm32(h+4)||!fm_parse(f,v->size,ptr,&raw,raws,sizeof raws/sizeof *raws))goto done;
    if(!number(&raw,256,11904)||!number(&raw,257,8842)||!number(&raw,258,16)||
       !number(&raw,259,1)||!number(&raw,262,32803)||!number(&raw,277,1)||
       !number(&raw,278,8842)||!number(&raw,284,1)||
       !integer(&raw,50719,crop,2)||crop[0]!=128||crop[1]!=96||
       !integer(&raw,50720,crop,2)||crop[0]!=11656||crop[1]!=8742||
       !integer(&raw,273,&ptr,1)||!integer(&raw,279,&length,1)||length!=210510336u||
       ptr<8||ptr>v->size||length>v->size-ptr||
       !integer(&raw,50717,&v->white,1)||v->white>65535||!v->white)goto done;
    v->offset=ptr;
    if(!integer(root,34665,&ptr,1)||!fm_parse(f,v->size,ptr,&exif,exifs,3)||
       !rational(&exif,33434,&v->exposure.tvn,&v->exposure.tvd)||
       !fm_aperture(&exif,&v->exposure.aperture_known,&v->exposure.avn,&v->exposure.avd)||
       !integer(&exif,34855,&v->exposure.iso,1)||!v->exposure.iso)goto done;
    /* X2D disk-route contract: scalar factory black, unchanged sample domain.
     * No optical-black estimation, no per-channel normalization fallback. */
    FmTag *black=fm_find(&raw,50714);
    if(!black||black->type!=5||black->count!=1||black->size!=8||
       fm32(black->data+4)!=1||fm32(black->data)>8192||
       fm32(black->data)>=v->white)goto done;
    for(unsigned c=0;c<4;c++)v->black[c]=fm32(black->data);
    struct stat after;
    if(fstat(fileno(f),&after)||st.st_size!=after.st_size||
       st.st_mtim.tv_sec!=after.st_mtim.tv_sec||st.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)goto done;
    ok=1;
done:fm_free(&raw);fm_free(&exif);return ok;
}
static unsigned char *header(FmIfd *first,const Input *range,unsigned *size){
    FmIfd d={0};unsigned char *result=NULL;
    const unsigned tags[]={256,257,258,259,262,273,277,278,279,284,50711,50714,50717};
    const uint32_t values[]={23310,17482,16,1,32803,0,1,17482,0,1,1,range->black[0],range->white};
    for(unsigned i=0;i<sizeof tags/sizeof *tags;i++){
        unsigned tag=tags[i];
        if(tag==258||tag==259||tag==262||tag==277||tag==284||tag==50711){
            unsigned char s[2];fmw16(s,(uint16_t)values[i]);if(!fm_put(&d,tag,3,1,s))goto done;
        }else if(!fm_number(&d,tag,tag==279?23310u*17482u*2u:values[i]))goto done;
    }
    unsigned char two[4]={2,0,2,0},one[4]={1,0,1,0},cfa[4]={1,0,2,1};
    unsigned char version[4]={1,4,0,0},back[4]={1,1,0,0},colors[3]={0,1,2},xy[8]={0};
    if(!fm_put(&d,33421,3,2,two)||!fm_put(&d,33422,1,4,cfa)||
       !fm_put(&d,50706,1,4,version)||!fm_put(&d,50707,1,4,back)||
       !fm_put(&d,50710,1,3,colors)||!fm_put(&d,50713,3,2,one)||
       !fm_put(&d,50719,4,2,xy))goto done;
    fmw32(xy,23310);fmw32(xy+4,17482);if(!fm_put(&d,50720,4,2,xy))goto done;
    unsigned copy[]={274,50708,50721,50728};
    for(unsigned i=0;i<4;i++)if(!fm_find(first,copy[i])||!fm_copy(&d,first,copy[i]))goto done;
    if(!fm_number(&d,50778,0)||!fm_string(&d,305,"X2DII experimental six-frame merge"))goto done;
    unsigned cursor=8+2+12*d.count+4,total=(cursor+fm_extra(&d)+15)&~15u;
    if(total>FM_LIMIT||!fm_number(&d,273,total))goto done;
    result=calloc(1,total);if(!result)goto done;
    memcpy(result,"II*\0",4);fmw32(result+4,8);fm_encode(result,8,&d,&cursor);
    if(cursor>total){free(result);result=NULL;goto done;}*size=total;
done:fm_free(&d);return result;
}
static int write_new(int dir,const char *name,const void *data,size_t bytes){
    int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
    const unsigned char *p=data;size_t done=0;int ok=1;
    while(done<bytes){ssize_t n=write(fd,p+done,bytes-done);if(n<=0){ok=0;break;}done+=(size_t)n;}
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    return ok; /* 失败保留诊断文件；不删除，不允许原地重试覆盖。 */
}
static int emit_six(int dir,Input inputs[6],FmIfd *first){
    unsigned char job[256]={0};unsigned bytes=0;
    unsigned char *base=header(first,&inputs[0],&bytes);if(!base)return 0;
    memcpy(job,"NX2DKEEP",8);uint32_t geometry[]={11904,8842,128,96,11656,8742,bytes,0};
    for(unsigned i=0;i<8;i++)fmw32(job+8+4*i,geometry[i]);
    for(unsigned i=0;i<6;i++){
        unsigned char *p=job+40+36*i;fmw32(p,(uint32_t)inputs[i].offset);fmw32(p+8,(uint32_t)inputs[i].size);
        for(unsigned c=0;c<4;c++)fmw32(p+16+c*4,inputs[i].black[c]);
        fmw32(p+32,inputs[i].white);
    }
    int ok=write_new(dir,"header.dng",base,bytes)&&!fsync(dir)&&write_new(dir,"job.bin",job,sizeof job)&&!fsync(dir);
    free(base);return ok;
}
int ps_prepare_six(const char *path){
    if(!path||path[0]!='/')return 2;
    int dir=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 2;
    Input inputs[6]={0};FmIfd roots[6]={0};struct stat identities[6];int rc=1;
    struct stat st;if(fstatat(dir,"job.bin",&st,AT_SYMLINK_NOFOLLOW)==0||fstatat(dir,"header.dng",&st,AT_SYMLINK_NOFOLLOW)==0)goto done;
    for(unsigned i=0;i<6;i++){
        char name[32];snprintf(name,sizeof name,"input%u.3fr",i);
        /* 私有任务目录可含受信协调器创建的只读来源链接；不删除链接目标。 */
        int fd=openat(dir,name,O_RDONLY|O_CLOEXEC);if(fd<0)goto done;
        FILE *f=fdopen(fd,"rb");if(!f){close(fd);goto done;}
        int ok=!fstat(fd,&identities[i])&&parse(f,&inputs[i],&roots[i]);fclose(f);if(!ok)goto done;
        for(unsigned k=0;k<i;k++)if((identities[k].st_dev==identities[i].st_dev&&identities[k].st_ino==identities[i].st_ino)||
            !exposure_equal(&inputs[k].exposure,&inputs[i].exposure))goto done;
    }
    for(unsigned i=1;i<6;i++)
        if(memcmp(inputs[i].black,inputs[0].black,sizeof inputs[0].black))goto done;
    if(!emit_six(dir,inputs,&roots[0]))goto done;
    puts("PREPARED_SIX_EXPOSURE_EQUAL_NO_CAPTURE_OR_DELETE");rc=0;
done:
    if(rc)fputs("PREPARE_FAILED_KEEP_ALL_INPUTS\n",stderr);
    for(unsigned i=0;i<6;i++)fm_free(&roots[i]);
    close(dir);return rc;
}
#ifndef PS_PREPARE_LIBRARY
int main(int argc,char **argv){
    if(argc!=3||strcmp(argv[1],"--prepare-six"))return 2;
    return ps_prepare_six(argv[2]);
}
#endif
