/* 自写、私有候选：有界 TIFF/Exif 元数据重封装，不复制 MakerNote 或旧图像指针。
 * 基础 DNG 结构仍来自已验证模板；此模块不替代 RAW 布局/校准解析器。 */
#ifndef FIRST_FRAME_METADATA_H
#define FIRST_FRAME_METADATA_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FM_LIMIT 65536u
#define FM_TAGS 128u
typedef struct { uint16_t id,type; uint32_t count,size; unsigned char *data; } FmTag;
typedef struct { FmTag tags[FM_TAGS]; unsigned count,total; } FmIfd;
static uint16_t fm16(const unsigned char *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t fm32(const unsigned char *p){return fm16(p)|(uint32_t)fm16(p+2)<<16;}
static void fmw16(unsigned char *p,uint16_t n){p[0]=n;p[1]=n>>8;}
static void fmw32(unsigned char *p,uint32_t n){fmw16(p,n);fmw16(p+2,n>>16);}
static unsigned fm_width(unsigned t){static const unsigned w[]={0,1,1,2,4,8,1,1,2,4,8,4,8};return t<13?w[t]:0;}
static void fm_free(FmIfd *d){for(unsigned i=0;i<d->count;i++)free(d->tags[i].data);memset(d,0,sizeof *d);}
static FmTag *fm_find(FmIfd *d,unsigned id){for(unsigned i=0;i<d->count;i++)if(d->tags[i].id==id)return d->tags+i;return NULL;}
static int fm_put(FmIfd *d,unsigned id,unsigned type,uint32_t count,const void *data){
    unsigned w=fm_width(type);uint64_t size=(uint64_t)count*w;
    if(!w||!count||size>FM_LIMIT||d->total+size>FM_LIMIT)return 0;
    unsigned char *copy=malloc((size_t)size);if(!copy)return 0;memcpy(copy,data,(size_t)size);
    FmTag *tag=fm_find(d,id);
    if(!tag){if(d->count==FM_TAGS){free(copy);return 0;}tag=&d->tags[d->count++];memset(tag,0,sizeof *tag);}
    d->total-=tag->size;free(tag->data);*tag=(FmTag){id,type,count,(uint32_t)size,copy};d->total+=(unsigned)size;return 1;
}
static int fm_number(FmIfd *d,unsigned id,uint32_t value){unsigned char p[4];fmw32(p,value);return fm_put(d,id,4,1,p);}
static int fm_string(FmIfd *d,unsigned id,const char *value){return fm_put(d,id,2,(uint32_t)strlen(value)+1,value);}
static int fm_read(FILE *f,uint64_t length,uint64_t off,void *p,size_t n){
    return off<=length&&n<=length-off&&!fseeko(f,(off_t)off,SEEK_SET)&&fread(p,1,n,f)==n;
}
static int fm_wanted(unsigned id,const unsigned *ids,size_t count){
    if(!ids)return 1;
    for(size_t i=0;i<count;i++)if(ids[i]==id)return 1;
    return 0;
}
static int fm_parse(FILE *f,uint64_t length,uint32_t off,FmIfd *d,const unsigned *ids,size_t wanted){
    unsigned char count[2],entry[12],tail[4];
    if(off<8||!fm_read(f,length,off,count,2))return 0;
    unsigned n=fm16(count);if(n>FM_TAGS)return 0;
    if(!fm_read(f,length,(uint64_t)off+2+12*n,tail,4))return 0;
    /* 只读取明确的一层目录；不追踪 NextIFD 或不明内部偏移。 */
    uint16_t seen[FM_TAGS];
    for(unsigned i=0;i<n;i++){
        if(!fm_read(f,length,(uint64_t)off+2+i*12,entry,12))return 0;
        unsigned id=fm16(entry),type=fm16(entry+2);uint32_t num=fm32(entry+4);
        for(unsigned k=0;k<i;k++)if(seen[k]==id)return 0;
        seen[i]=(uint16_t)id;
        if(!fm_wanted(id,ids,wanted))continue;
        unsigned w=fm_width(type);uint64_t bytes=(uint64_t)w*num;
        if(!w||!num||bytes>FM_LIMIT||d->total+bytes>FM_LIMIT)return 0;
        unsigned char *p=malloc((size_t)bytes);if(!p)return 0;
        int ok=1;
        if(bytes<=4)memcpy(p,entry+8,(size_t)bytes);
        else ok=fm_read(f,length,fm32(entry+8),p,(size_t)bytes);
        /* Observed factory no-electronics lens sentinel: SubjectDistance 0/0.
         * Omit this unknown optional field; never relax other rationals or
         * nonzero/zero distances, and never manufacture a distance value. */
        if(ok&&id==37382&&type==5&&num==1&&bytes==8&&
           fm32(p)==0&&fm32(p+4)==0){free(p);continue;}
        if(ok&&type==2)ok=p[bytes-1]==0;
        if(ok&&(type==5||type==10))for(uint32_t j=0;j<num;j++)if(!fm32(p+j*8+4)){ok=0;break;}
        if(ok)ok=fm_put(d,id,type,num,p);
        free(p);if(!ok)return 0;
    }
    return 1;
}
/* Missing FNumber, or 0 with a valid denominator, means unknown, not f/0.
 * Preserve the source tag when packing. Never accept malformed rationals. */
static int fm_aperture(FmIfd *d,uint32_t *known,uint32_t *n,uint32_t *den){
    *known=*n=*den=0;
    FmTag *t=fm_find(d,33437);if(!t)return 1;
    if(t->type!=5||t->count!=1||t->size!=8||!t->data)return 0;
    uint32_t a=fm32(t->data),b=fm32(t->data+4);if(!b)return 0;
    if(!a)return 1;
    uint32_t x=a,y=b;while(y){uint32_t r=x%y;x=y;y=r;}
    *known=1;*n=a/x;*den=b/x;return 1;
}
static int fm_aperture_valid(FmIfd *d){uint32_t k,n,b;return fm_aperture(d,&k,&n,&b);}
static int fm_copy(FmIfd *to,FmIfd *from,unsigned id){FmTag *t=fm_find(from,id);return !t||fm_put(to,id,t->type,t->count,t->data);}
static int fm_cmp(const void *a,const void *b){const FmTag *x=a,*y=b;return (x->id>y->id)-(x->id<y->id);}
static unsigned fm_extra(const FmIfd *d){unsigned n=0;for(unsigned i=0;i<d->count;i++)if(d->tags[i].size>4)n+=(d->tags[i].size+1)&~1u;return n;}
static void fm_encode(unsigned char *out,unsigned off,FmIfd *d,unsigned *cursor){
    qsort(d->tags,d->count,sizeof(FmTag),fm_cmp);fmw16(out+off,(uint16_t)d->count);
    for(unsigned i=0;i<d->count;i++){
        FmTag *t=d->tags+i;unsigned char *p=out+off+2+12*i;
        fmw16(p,t->id);fmw16(p+2,t->type);fmw32(p+4,t->count);
        if(t->size<=4)memcpy(p+8,t->data,t->size);
        else{fmw32(p+8,*cursor);memcpy(out+*cursor,t->data,t->size);*cursor+=(t->size+1)&~1u;}
    }
}
/* 成功返回一份新头；失败没有任何文件写入。只支持已验证的 little-endian TIFF。 */
static unsigned char *first_frame_header(FILE *first,uint64_t first_size,FILE *base,unsigned old_size,
                                         unsigned width,unsigned height,unsigned *new_size){
    static const unsigned root_ids[]={271,272,274,306,315,33432,34665,50708,50721,50728,50735,50778};
    static const unsigned exif_ids[]={33434,33437,34850,34855,34864,34865,34866,34867,36864,36867,36868,36880,36881,36882,
        37377,37378,37379,37380,37381,37382,37383,37384,37385,37386,37520,37521,37522,40960,41486,41487,41488,41986,41987,41989,42032,42033,42034,42035,42036,42037};
    static const unsigned structural_ids[]={256,257,258,259,262,273,274,277,278,279,284,305,33421,33422,50706,50707,50708,50710,50711,50713,50714,50717,50719,50720,50721,50728,50778};
    FmIfd *root=calloc(1,sizeof *root),*src=calloc(1,sizeof *src),*exif=calloc(1,sizeof *exif);
    unsigned char h[8],*out=NULL;
    if(!root||!src||!exif||old_size>FM_LIMIT||!width||!height)goto done;
    if(!fm_read(base,old_size,0,h,8)||memcmp(h,"II*\0",4))goto done;
    if(!fm_parse(base,old_size,fm32(h+4),root,structural_ids,sizeof structural_ids/sizeof *structural_ids))goto done;
    if(!fm_read(first,first_size,0,h,8)||memcmp(h,"II*\0",4))goto done;
    uint32_t source_root=fm32(h+4);
    if(!fm_parse(first,first_size,source_root,src,root_ids,sizeof root_ids/sizeof *root_ids))goto done;
    FmTag *ptr=fm_find(src,34665);
    if(!ptr||ptr->type!=4||ptr->count!=1||fm32(ptr->data)==source_root)goto done;
    if(!fm_parse(first,first_size,fm32(ptr->data),exif,exif_ids,sizeof exif_ids/sizeof *exif_ids))goto done;
    unsigned required[]={33434,34855};
    if(!fm_aperture_valid(exif))goto done;
    for(unsigned i=0;i<2;i++){
        FmTag *tag=fm_find(exif,required[i]);
        if(!tag||tag->count!=1||tag->type!=(i==0?5u:3u))goto done;
    }
    for(unsigned i=0;i<sizeof root_ids/sizeof *root_ids;i++)if(root_ids[i]!=34665&&!fm_copy(root,src,root_ids[i]))goto done;
    FmTag *orientation=fm_find(root,274);
    if(!orientation||orientation->type!=3||orientation->count!=1||fm16(orientation->data)<1||fm16(orientation->data)>8)goto done;
    if(!fm_number(exif,40962,width)||!fm_number(exif,40963,height)||
       !fm_number(root,256,width)||!fm_number(root,257,height)||!fm_number(root,34665,0)||
       !fm_string(root,305,"X2DII experimental six-frame merge")||
       !fm_string(root,270,"Six-frame pixel-shift synthesis; first-frame capture metadata"))goto done;
    unsigned exif_off=8+2+12*root->count+4;
    unsigned cursor=exif_off+2+12*exif->count+4;
    unsigned total=(cursor+fm_extra(root)+fm_extra(exif)+15)&~15u;
    if(total>FM_LIMIT||!fm_number(root,34665,exif_off))goto done;
    FmTag *offsets=fm_find(root,273);
    if(!offsets||offsets->type!=4)goto done;
    for(uint32_t i=0;i<offsets->count;i++){
        uint32_t previous=fm32(offsets->data+4*i);
        if(previous<old_size||(uint64_t)previous-old_size+total>UINT32_MAX)goto done;
        fmw32(offsets->data+4*i,previous-old_size+total);
    }
    out=calloc(1,total);if(!out)goto done;
    memcpy(out,"II*\0",4);fmw32(out+4,8);
    fm_encode(out,8,root,&cursor);fm_encode(out,exif_off,exif,&cursor);
    if(cursor>total){free(out);out=NULL;goto done;}*new_size=total;
done:
    if(root){fm_free(root);free(root);}if(src){fm_free(src);free(src);}if(exif){fm_free(exif);free(exif);}
    return out;
}
#endif
