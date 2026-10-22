/* 自写私有文件适配层；MIT 行核见本仓库 six_row_kernel.h。
 * 只读取显式离线任务目录；无 USB、Camera、属性或设备节点访问。
 * JOB 元数据由受信主机构建器生成，不是通用 3FR 解码器。 */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include "six_row_kernel.h"
#include "first_frame_metadata.h"
#include "readback_digest.h"

#define MAX_W 11904u
#define MAX_H 8842u
#define CACHE_ROWS 32u
typedef struct { FILE *f; uint64_t offset, bytes; unsigned black[4], white; uint16_t *lut;
    uint16_t *rows; unsigned first,count; } Frame;
typedef struct { unsigned sw,sh,left,top,cw,ch,header; Frame frame[6]; } Job;
static volatile sig_atomic_t cancelled;
static void on_signal(int number) { (void)number; cancelled=1; }
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t u64(const unsigned char *p) { return u32(p)|((uint64_t)u32(p+4)<<32); }
static unsigned u16(const unsigned char *p) { return (unsigned)p[0]|(unsigned)p[1]<<8; }
static int tiff_values(const unsigned char *p,size_t size,unsigned tag,uint32_t *values,unsigned cap,unsigned *count) {
    if(size<8 || memcmp(p,"II*\0",4))return 0;
    uint32_t off=u32(p+4);
    if(off>size-2)return 0;
    unsigned n=u16(p+off),found=0;
    if((uint64_t)off+2+(uint64_t)n*12+4>size)return 0;
    for(unsigned i=0;i<n;i++) {
        const unsigned char *e=p+off+2+i*12;
        if(u16(e)!=tag)continue;
        if(found++)return 0;
        unsigned type=u16(e+2),width=type==3?2:type==4?4:0;
        uint32_t cnt=u32(e+4);
        if(!width || !cnt || cnt>cap)return 0;
        uint64_t bytes=(uint64_t)width*cnt;
        const unsigned char *v=e+8;
        if(bytes>4) { uint32_t pos=u32(v);if((uint64_t)pos+bytes>size)return 0;v=p+pos; }
        for(unsigned k=0;k<cnt;k++)values[k]=width==2?u16(v+k*width):u32(v+k*width);
        *count=cnt;
    }
    return found==1;
}
static int validate_header(const unsigned char *p,Job *j) {
    unsigned tags[6]={256,257,258,259,277,278},expect[5]={2*(j->cw-1),2*(j->ch-1),16,1,1};
    unsigned cnt=0;uint32_t value=0;
    for(unsigned i=0;i<6;i++) {
        if(!tiff_values(p,j->header,tags[i],&value,1,&cnt) || cnt!=1)return 0;
        if(i<5 && value!=expect[i])return 0;
    }
    if(!value || value>expect[1])return 0;
    unsigned strips=(expect[1]+value-1)/value;
    if(strips>MAX_H*2)return 0;
    uint32_t *offsets=malloc(strips*sizeof(uint32_t)),*sizes=malloc(strips*sizeof(uint32_t));
    if(!offsets || !sizes){free(offsets);free(sizes);return 0;}
    int ok=tiff_values(p,j->header,273,offsets,strips,&cnt) && cnt==strips &&
        tiff_values(p,j->header,279,sizes,strips,&cnt) && cnt==strips;
    uint64_t next=j->header;
    for(unsigned i=0;ok && i<strips;i++) {
        unsigned rows=expect[1]-i*value;if(rows>value)rows=value;
        ok=offsets[i]==next && sizes[i]==(uint64_t)rows*expect[0]*2;
        next+=sizes[i];
    }
    free(offsets);free(sizes);return ok;
}
static int path_join(char out[4096],const char *dir,const char *name) {
    return snprintf(out,4096,"%s/%s",dir,name)<4096;
}
/* Keep EVERY uint16 input, including below-black / above-white samples.
 * The existing six-position kernel still performs its normal interpolation. */
static int prepare_luts(Job *j) {
    if(!j||!j->frame[0].white||j->frame[0].white>65535)return 0;
    unsigned b=j->frame[0].black[0];
    if(b>8192||b>=j->frame[0].white)return 0;
    for(unsigned i=0;i<6;i++){
        if(j->frame[i].white<=b||j->frame[i].white>65535)return 0;
        for(unsigned c=0;c<4;c++)if(j->frame[i].black[c]!=b)return 0;
    }
    return !cancelled;
}
static uint16_t sample(const Frame *f,unsigned c,unsigned value) {
    (void)f;(void)c;return (uint16_t)value;
}
static int load(Job *j,const char *dir) {
    char path[4096]; unsigned char data[256];
    if(!path_join(path,dir,"job.bin"))return 0;
    FILE *f=fopen(path,"rb"); if(!f)return 0;
    int ok=fread(data,1,sizeof data,f)==sizeof data && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    if(!ok || memcmp(data,"NX2DKEEP",8))return 0;
    j->sw=u32(data+8);j->sh=u32(data+12);j->left=u32(data+16);j->top=u32(data+20);
    j->cw=u32(data+24);j->ch=u32(data+28);j->header=u32(data+32);
    if(u32(data+36) || j->sw<2 || j->sw>MAX_W || j->sh<2 || j->sh>MAX_H ||
       j->cw<2 || j->cw>j->sw || j->ch<2 || j->ch>j->sh ||
       j->left>j->sw-j->cw || j->top>j->sh-j->ch || (j->left&1) || (j->top&1) ||
       j->header<8 || j->header>1048576)return 0;
    for(unsigned i=0;i<6;i++) {
        Frame *v=j->frame+i; const unsigned char *p=data+40+36*i;
        v->offset=u64(p);v->bytes=u64(p+8);
        for(unsigned c=0;c<4;c++)v->black[c]=u32(p+16+c*4);
        v->white=u32(p+32);
        if(!v->white || v->white>65535 || v->offset>UINT32_MAX || v->bytes>UINT32_MAX)return 0;
        for(unsigned c=0;c<4;c++)if(v->black[c]>=v->white)return 0;
        uint64_t raster=(uint64_t)j->sw*j->sh*2;
        if(v->bytes<v->offset || v->bytes-v->offset<raster)return 0;
        char name[32];snprintf(name,sizeof name,"input%u.3fr",i);
        if(!path_join(path,dir,name))return 0;
        v->f=fopen(path,"rb");if(!v->f)return 0;
        struct stat st;
        if(fstat(fileno(v->f),&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=v->bytes)return 0;
    }
    return 1;
}
static int read_row(Job *j,unsigned i,unsigned y,uint16_t *row) {
    Frame *f=j->frame+i;
    if(y>=j->ch || cancelled)return 0;
#ifndef PS_REFERENCE_IO
    if(!f->rows){f->rows=malloc((size_t)CACHE_ROWS*j->sw*2);if(!f->rows)return 0;}
    if(!f->count||y<f->first||y>=f->first+f->count){
        unsigned count=j->ch-y;if(count>CACHE_ROWS)count=CACHE_ROWS;
        uint64_t off=f->offset+(uint64_t)(j->top+y)*j->sw*2;
        size_t pixels=(size_t)count*j->sw;
        if(fseeko(f->f,(off_t)off,SEEK_SET)||fread(f->rows,2,pixels,f->f)!=pixels)return 0;
        f->first=y;f->count=count;
    }
    memcpy(row,f->rows+(size_t)(y-f->first)*j->sw+j->left,j->cw*2);
#else
    uint64_t off=f->offset+((uint64_t)(j->top+y)*j->sw+j->left)*2;
    if(fseeko(f->f,(off_t)off,SEEK_SET) || fread(row,2,j->cw,f->f)!=j->cw)return 0;
#endif
    return 1;
}
static unsigned channel(unsigned x,unsigned y) {
    static const unsigned cfa[2][2]={{0,1},{3,2}};return cfa[y&1][x&1];
}
static int integer_row(Job *j,unsigned y,uint16_t dst[][4],uint16_t *buf) {
    static const unsigned dx[4]={0,0,1,1},dy[4]={0,1,0,1};
    for(unsigned i=0;i<4;i++) {
        unsigned sy=y+1-dy[i];if(!read_row(j,i,sy,buf))return 0;
        for(unsigned x=0;x<j->cw-1;x++) {
            unsigned c=channel(x+dx[i],sy);
            dst[x][c]=sample(j->frame+i,c,buf[x+dx[i]]);
        }
    }
    return 1;
}
static int half_row(Job *j,unsigned y,uint16_t dst[][3],uint8_t *valid,uint16_t *buf) {
    unsigned w=j->cw-1;memset(dst,0,w*6);memset(valid,0,w);
    for(unsigned i=4;i<6;i++) {
        unsigned sy=y+(i==4);if(!read_row(j,i,sy,buf))return 0;
        for(unsigned x=0;x<w;x++) {
            unsigned c=channel(x+1,sy),out=c==3?1:c;
            if(valid[x]&(1u<<out))return 0;
            dst[x][out]=sample(j->frame+i,c,buf[x+1]);
            valid[x]|=(uint8_t)(1u<<out);
        }
    }
    return 1;
}
static double seconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;
}
#ifdef PS_CHUNK_PIPELINE
#include "overlap_chunk_pipeline.h"
#endif
static int run(const char *dir,const char *output,int inherit_metadata) {
    Job j={0};FILE *out=NULL,*head=NULL;void *scratch=NULL;int rc=1,owned=0,dirfd=-1;
    unsigned char *rebuilt_header=NULL;
    char partial[4096],path[4096];double begin=seconds();
    uint16_t endian=1;
    if(*(unsigned char*)&endian!=1 || snprintf(partial,sizeof partial,"%s.partial",output)>=(int)sizeof partial)return 2;
    if(!load(&j,dir)){fputs("INVALID_JOB_OR_INPUT\n",stderr);goto done;}
    if(!prepare_luts(&j))goto done;
    if(access(output,F_OK)==0){fputs("OUTPUT_EXISTS\n",stderr);goto done;}
    if(strlen(output)>=sizeof path)goto done;
    strcpy(path,output);
    char *slash=strrchr(path,'/');
    if(slash){if(slash==path)slash[1]=0;else *slash=0;}else strcpy(path,".");
    dirfd=open(path,O_RDONLY|O_DIRECTORY);if(dirfd<0)goto done;
    if(!path_join(path,dir,"header.dng"))goto done;
    head=fopen(path,"rb");if(!head)goto done;
    unsigned w=j.cw-1,h=j.ch-1;
    if(inherit_metadata){
        unsigned new_size=0;
        rebuilt_header=first_frame_header(j.frame[0].f,j.frame[0].bytes,head,j.header,w*2,h*2,&new_size);
        if(!rebuilt_header){fputs("FIRST_FRAME_METADATA_INVALID\n",stderr);goto done;}
        j.header=new_size;
    }
    size_t bytes=(size_t)w*38+j.cw*2+j.header;
    scratch=calloc(1,bytes);if(!scratch)goto done;
    unsigned char *p=scratch;
    uint16_t (*a)[4]=(void*)p;p+=w*8;
    uint16_t (*b)[4]=(void*)p;p+=w*8;
    uint16_t (*cur)[3]=(void*)p;p+=w*6;
    uint16_t (*prev)[3]=(void*)p;p+=w*6;
    uint16_t *result=(void*)p;p+=w*8;
    uint16_t *row=(void*)p;p+=j.cw*2;
    uint8_t *cv=p;p+=w;uint8_t *pv=p;p+=w;
    unsigned char *header=p;
    if(rebuilt_header)memcpy(header,rebuilt_header,j.header);
    else if(fread(header,1,j.header,head)!=j.header || fgetc(head)!=EOF || ferror(head))goto done;
    if(!validate_header(header,&j))goto done;
    free(rebuilt_header);rebuilt_header=NULL;
    fclose(head);head=NULL;
    out=fopen(partial,"wbx");if(!out)goto done;owned=1;
    PsReadbackDigest digest;ps_digest_init(&digest);
    if(fwrite(header,1,j.header,out)!=j.header)goto done;
    ps_digest_update(&digest,header,j.header);
    double setup_end=seconds(),rows_end=0,sync_end=0,readback_end=0;
#ifdef PS_CHUNK_PIPELINE
    (void)a;(void)b;(void)cur;(void)prev;(void)result;(void)row;(void)cv;(void)pv;
    if(!chunk_pipeline(&j,out,&digest))goto done;
#else
    if(!integer_row(&j,0,a,row) || !half_row(&j,0,prev,pv,row))goto done;
    for(unsigned y=0;y<h;y++) {
        if(cancelled || !integer_row(&j,y+1<h?y+1:y,b,row) || !half_row(&j,y,cur,cv,row))goto done;
        SixNativeRow ctx={w,(const uint16_t(*)[4])a,(const uint16_t(*)[4])b,
            (const uint16_t(*)[3])cur,(const uint16_t(*)[3])prev,cv,pv,result};
        six_native_range(0,w,&ctx);
        if(fwrite(result,8,w,out)!=w)goto done;
        ps_digest_update(&digest,result,w*8);
        uint16_t (*swap4)[4]=a;a=b;b=swap4;
        uint16_t (*swap3)[3]=prev;prev=cur;cur=swap3;
        uint8_t *swap8=pv;pv=cv;cv=swap8;
        if(y%1024==0){printf("NATIVE_PROGRESS %u/%u\n",y,h);fflush(stdout);}
    }
#endif
    rows_end=seconds();
    if(cancelled || fflush(out) || fsync(fileno(out)))goto done;
    {int closed=fclose(out);out=NULL;if(closed)goto done;}
    sync_end=seconds();
    out=fopen(partial,"rb");if(!out)goto done;
    PsReadbackDigest readback;ps_digest_init(&readback);uint64_t count=0;size_t got;
    while((got=fread(scratch,1,bytes,out))>0) {
        if(cancelled)goto done;
        ps_digest_update(&readback,scratch,got);count+=got;
    }
    if(ferror(out) || count!=(uint64_t)j.header+(uint64_t)w*h*8 || !ps_digest_equal(&digest,&readback))goto done;
    fclose(out);out=NULL;
    readback_end=seconds();
    /* Linux 原子不覆盖发布；不降级为可能覆盖既有文件的 rename。 */
    struct rusage usage;if(getrusage(RUSAGE_SELF,&usage))goto done;
    if(cancelled || syscall(SYS_renameat2,AT_FDCWD,partial,AT_FDCWD,output,1))goto done;
    owned=0;
    if(fsync(dirfd)){fputs("PUBLISHED_BUT_DIRECTORY_DURABILITY_UNCONFIRMED\n",stderr);goto done;}
    printf("NATIVE_METRICS pixels=%llu bytes=%llu seconds=%.3f maxrss_kib=%ld scratch_bytes=%zu\n",
        (unsigned long long)w*h*4,(unsigned long long)count,seconds()-begin,usage.ru_maxrss,bytes);
    printf("NATIVE_TIMING setup=%.3f rows=%.3f sync=%.3f readback=%.3f publish=%.3f\n",
        setup_end-begin,rows_end-setup_end,sync_end-rows_end,readback_end-sync_end,seconds()-readback_end);
    puts("NATIVE_FILE_MERGE_READBACK_PASS");rc=0;
done:
    if(rc)fprintf(stderr,"NATIVE_FAILED cancelled=%d errno=%d\n",cancelled,errno);
    if(out)fclose(out);
    if(head)fclose(head);
    if(dirfd>=0)close(dirfd);
    for(unsigned i=0;i<6;i++){if(j.frame[i].f)fclose(j.frame[i].f);free(j.frame[i].lut);free(j.frame[i].rows);}
    free(scratch);
    free(rebuilt_header);
    if(owned)unlink(partial); /* 只清理本次独占创建的精确临时文件。 */
    return cancelled?130:rc;
}
int main(int argc,char **argv) {
    int inherit_metadata=argc==4&&!strcmp(argv[1],"--first-frame-metadata");
    if(argc!=3&&!inherit_metadata){fputs("Usage: six-file-worker [--first-frame-metadata] JOB_DIR NEW_OUTPUT.dng\n",stderr);return 2;}
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    return run(argv[1+inherit_metadata],argv[2+inherit_metadata],inherit_metadata);
}
