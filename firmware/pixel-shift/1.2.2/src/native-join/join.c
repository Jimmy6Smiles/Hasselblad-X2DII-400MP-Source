/* 原厂分块 JPEG 的压缩域拼接。重排行 MCU，重写 DC 差值和重启标记。
 * 不做 IDCT、不生成像素、不再量化；保留原厂 DQT/DHT 与 AC 数据。
 * 输入属于本任务；失败保留全部输入，移除本程序新建的中间输出。 */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "factory_jpeg_band_header.h"
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#undef fseeko
#undef ftello
#define fseeko _fseeki64
#define ftello _ftelli64
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
typedef struct {
 unsigned count[17],code[17],at[17];uint8_t symbols[256],size[256];uint16_t encoding[256],quick[1024];int present;
} Huff;
typedef struct {
 FILE *f;uint8_t header[4096];FjbHeader h;Huff dc[4],ac[4];
 unsigned dc_table[3],ac_table[3],horizontal[3],vertical[3];
 int predictors[3],bad,ended;uint32_t bits;unsigned available;
} Piece;
typedef struct {FILE *f;uint32_t bits;unsigned available;int bad,predictors[3];unsigned mcus,restart;} Output;
static unsigned tile_offset;static Piece sources[16];static uint8_t first[4096];static FjbHeader first_info;
static unsigned u16(const uint8_t *p){return (unsigned)p[0]*256+p[1];}
static int huffman(Huff *h,const uint8_t *p,unsigned length,unsigned *used){
 if(length<17||h->present)return 0;
 unsigned count=0,code=0;memset(h,0,sizeof *h);
 for(unsigned n=1;n<=16;n++){
  unsigned k=p[n];if(code+k>(1u<<n)||count+k>256)return 0;
  h->count[n]=k;h->code[n]=code;h->at[n]=count;count+=k;code=(code+k)*2;
 }
 if(!count||length<17+count)return 0;
 for(unsigned n=1;n<=16;n++)for(unsigned k=0;k<h->count[n];k++){
  unsigned v=p[17+h->at[n]+k];if(h->size[v])return 0;
  h->symbols[h->at[n]+k]=(uint8_t)v;h->size[v]=(uint8_t)n;h->encoding[v]=(uint16_t)(h->code[n]+k);
 }
 for(unsigned v=0;v<256;v++)if(h->size[v]&&h->size[v]<=10){
  unsigned shift=10-h->size[v],base=(unsigned)h->encoding[v]<<shift;
  for(unsigned i=0;i<(1u<<shift);i++)h->quick[base+i]=(uint16_t)((h->size[v]<<8)|v);
 }
 h->present=1;*used=17+count;return 1;
}
static int piece_open(Piece *s,const char *path){
 memset(s,0,sizeof *s);int fd=open(path,O_RDONLY|O_BINARY|O_NOFOLLOW);if(fd<0)return 0;
 s->f=fdopen(fd,"rb");if(!s->f){close(fd);return 0;}
 if(fseeko(s->f,-2,SEEK_END)||getc_unlocked(s->f)!=255||getc_unlocked(s->f)!=217||fseeko(s->f,0,SEEK_SET))return 0;
 size_t bytes=fread(s->header,1,sizeof s->header,s->f);if(bytes<64)return 0;
 /* 只交给既有小头解析器实际头部；已单独检查文件尾 EOI。 */
 s->header[4094]=255;s->header[4095]=217;
 if(!fjb_parse(s->header,sizeof s->header,&s->h)||s->h.scan>bytes)return 0;
 for(unsigned pos=2;pos<s->h.sos;){
  unsigned marker=s->header[pos+1],len=u16(s->header+pos+2);
  if(marker==0xc0){for(unsigned c=0;c<3;c++){
   unsigned sample=s->header[pos+11+3*c];s->horizontal[c]=sample>>4;s->vertical[c]=sample&15;
   if(s->header[pos+10+3*c]!=s->header[s->h.sos+5+2*c])return 0;
  }}
  if(marker==0xc4){unsigned at=pos+4,end=pos+2+len;
   while(at<end){unsigned sel=s->header[at],id=sel&15,type=sel>>4,n;
    if(id>3||type>1||!huffman(type?s->ac+id:s->dc+id,s->header+at,end-at,&n))return 0;at+=n;}
  }
  pos+=2+len;
 }
 for(unsigned c=0;c<3;c++){
  unsigned sel=s->header[s->h.sos+6+2*c];s->dc_table[c]=sel>>4;s->ac_table[c]=sel&15;
  if(s->dc_table[c]>3||s->ac_table[c]>3||!s->dc[s->dc_table[c]].present||!s->ac[s->ac_table[c]].present)return 0;
 }
 return fseeko(s->f,s->h.scan,SEEK_SET)==0;
}
static int compatible(const Piece *s){
 if(s->h.scan!=first_info.scan||s->h.sof_height!=first_info.sof_height||s->h.sos!=first_info.sos)return 0;
 for(unsigned i=0;i<s->h.scan;i++)if((i<first_info.sof_height||i>first_info.sof_height+3)&&s->header[i]!=first[i])return 0;
 return 1;
}
static void refill(Piece*s,unsigned n){
 while(s->available<n&&!s->ended&&!s->bad){int byte=getc_unlocked(s->f);
  if(byte<0){s->bad=1;break;}
  if(byte==255){int marker=getc_unlocked(s->f);if(marker==217){s->ended=1;break;}if(marker!=0){s->bad=1;break;}}
  s->bits=(s->bits<<8)|(unsigned)byte;s->available+=8;
 }
}
static unsigned read_bits(Piece *s,unsigned n){
 if(n>16||s->bad){s->bad=1;return 0;}
 refill(s,n);if(s->bad||s->available<n){s->bad=1;return 0;}
 s->available-=n;return (s->bits>>s->available)&((1u<<n)-1);
}
static unsigned symbol(Piece *s,const Huff *h){
 refill(s,10);if(s->bad)return 0;
 if(s->available>=10){unsigned q=h->quick[(s->bits>>(s->available-10))&1023];
  if(q){s->available-=q>>8;return q&255;}}
 unsigned code=0;
 for(unsigned n=1;n<=16&&!s->bad;n++){
  code=(code<<1)|read_bits(s,1);
  if(code>=h->code[n]&&code-h->code[n]<h->count[n])return h->symbols[h->at[n]+code-h->code[n]];
 }
 s->bad=1;return 0;
}
static void write_byte(Output *o,unsigned b){if(putc_unlocked((int)b,o->f)<0||(b==255&&putc_unlocked(0,o->f)<0))o->bad=1;}
static void write_bits(Output *o,unsigned value,unsigned n){
 if(n>16){o->bad=1;return;}o->bits=(o->bits<<n)|(value&((1u<<n)-1));o->available+=n;
 while(o->available>=8){o->available-=8;write_byte(o,(o->bits>>o->available)&255);}
}
static void emit_symbol(Output *o,const Huff *h,unsigned v){if(v>255||!h->size[v])o->bad=1;else write_bits(o,h->encoding[v],h->size[v]);}
static void flush_bits(Output *o){if(o->available)write_bits(o,(1u<<(8-o->available))-1,8-o->available);}
static int block(Piece *s,Output *o,unsigned c,int emit){
 const Huff *dc=s->dc+s->dc_table[c],*ac=s->ac+s->ac_table[c];
 unsigned n=symbol(s,dc);if(n>11){s->bad=1;return 0;}
 unsigned bits=read_bits(s,n);int delta=(int)bits;
 if(n&&bits<(1u<<(n-1)))delta-=(1<<n)-1;
 s->predictors[c]+=delta;if(s->predictors[c]<-2048||s->predictors[c]>2047){s->bad=1;return 0;}
 if(emit){
  int d=s->predictors[c]-o->predictors[c];o->predictors[c]=s->predictors[c];
  unsigned value=(unsigned)(d<0?-d:d),size=0;while(value){value>>=1;size++;}
  if(size>11){o->bad=1;return 0;}
  emit_symbol(o,dc,size);if(size)write_bits(o,d<0?(unsigned)(d+(1<<size)-1):(unsigned)d,size);
 }
 unsigned at=1;
 while(at<64&&!s->bad){
  unsigned v=symbol(s,ac),run=v>>4,size=v&15;
  if(!size){if(v==0){if(emit)emit_symbol(o,ac,0);break;}
   if(v!=240||at+16>64){s->bad=1;return 0;}at+=16;if(emit)emit_symbol(o,ac,v);continue;}
  if(size>10||at+run>=64){s->bad=1;return 0;}at+=run+1;
  unsigned amplitude=read_bits(s,size);if(emit){emit_symbol(o,ac,v);write_bits(o,amplitude,size);}
 }
 return !s->bad&&!o->bad;
}
static unsigned gcd(unsigned a,unsigned b){while(b){unsigned r=a%b;a=b;b=r;}return a;}
static int serial_main(int argc,char **argv){
 if(argc!=7){fputs("expected output, job, width, height, step_x, step_y\n",stderr);return 2;}
 unsigned v[4];for(unsigned i=0;i<4;i++){char *end;unsigned long n=strtoul(argv[3+i],&end,10);
  if(*end||!n||n>65535)return 2;v[i]=(unsigned)n;}
 unsigned width=v[0],height=v[1],stepx=v[2],stepy=v[3];
 unsigned cols=(width+stepx-1)/stepx,grid_rows=(height+stepy-1)/stepy;
 if(cols>16||grid_rows>64)return 2;
 int rc=1,owned=0;Output out={0};unsigned interval=0;
 for(unsigned grid=0;grid<grid_rows;grid++){
  for(unsigned c=0;c<cols;c++){
   char path[512];int n=snprintf(path,sizeof path,"%s/render-tile-%u.jpg",argv[2],tile_offset+grid*cols+c);
   if(n<1||n>=(int)sizeof path||!piece_open(sources+c,path))goto end;
   Piece *s=sources+c;
   if(!grid&&!c){
    first_info=s->h;memcpy(first,s->header,s->h.scan);
    if(stepx%s->h.mcu_width||stepy%s->h.mcu_height)goto end;
    interval=gcd((width+s->h.mcu_width-1)/s->h.mcu_width,stepx/s->h.mcu_width);
    if(!interval||interval>65535)goto end;
   }else if(!compatible(s))goto end;
   unsigned kept_w=width-c*stepx;if(kept_w>stepx)kept_w=stepx;
   unsigned kept_h=height-grid*stepy;if(kept_h>stepy)kept_h=stepy;
   if(s->h.width<kept_w||s->h.height<kept_h||s->h.mcu_width!=first_info.mcu_width||s->h.mcu_height!=first_info.mcu_height)goto end;
  }
  if(!grid){
   int fd=open(argv[1],O_CREAT|O_EXCL|O_WRONLY|O_BINARY|O_NOFOLLOW,0600);if(fd<0)goto end;owned=1;
   out.f=fdopen(fd,"wb");if(!out.f){close(fd);goto end;}
   first[first_info.sof_height]=(uint8_t)(height>>8);first[first_info.sof_height+1]=(uint8_t)height;
   first[first_info.sof_height+2]=(uint8_t)(width>>8);first[first_info.sof_height+3]=(uint8_t)width;
   uint8_t dri[6]={255,221,0,4,(uint8_t)(interval>>8),(uint8_t)interval};
   if(fwrite(first,1,first_info.sos,out.f)!=first_info.sos||fwrite(dri,1,6,out.f)!=6||
      fwrite(first+first_info.sos,1,first_info.scan-first_info.sos,out.f)!=first_info.scan-first_info.sos)goto end;
  }
  unsigned rows=height-grid*stepy;if(rows>stepy)rows=stepy;
  rows=(rows+first_info.mcu_height-1)/first_info.mcu_height;
  for(unsigned row=0;row<rows;row++){
   for(unsigned c=0;c<cols;c++){
   Piece *s=sources+c;unsigned keep=width-c*stepx;if(keep>stepx)keep=stepx;
   keep=(keep+s->h.mcu_width-1)/s->h.mcu_width;
   unsigned total=(s->h.width+s->h.mcu_width-1)/s->h.mcu_width;
   for(unsigned x=0;x<total;x++){
    int emit=x<keep;
    if(emit&&out.mcus&&out.mcus%interval==0){
     flush_bits(&out);if(putc_unlocked(255,out.f)<0||putc_unlocked((int)(208+out.restart%8),out.f)<0)goto end;
     out.restart++;memset(out.predictors,0,sizeof out.predictors);
    }
    for(unsigned component=0;component<3;component++)for(unsigned n=0;n<s->horizontal[component]*s->vertical[component];n++)
     if(!block(s,&out,component,emit))goto end;
    if(emit)out.mcus++;
   }
   }
   /* 已完成整行压缩块后报告；不解码像素，不为进度刷盘。 */
   if((row+1)%64==0||row+1==rows){
    unsigned completed=grid*stepy+(row+1)*first_info.mcu_height;
    if(completed>height)completed=height;
    printf("JPEG_GRID_COMPRESSED_ROWS %u/%u\n",completed,height);fflush(stdout);
   }
  }
  for(unsigned c=0;c<cols;c++){if(fclose(sources[c].f)){sources[c].f=NULL;goto end;}sources[c].f=NULL;}
 }
 flush_bits(&out);
 if(out.bad||putc_unlocked(255,out.f)<0||putc_unlocked(217,out.f)<0||fflush(out.f)||fsync(fileno(out.f)))goto end;
 rc=0;
end:
 for(unsigned c=0;c<16;c++)if(sources[c].f&&fclose(sources[c].f))rc=1;
 if(out.f&&fclose(out.f))rc=1;
 if(rc&&owned)remove(argv[1]);
 puts(rc?"JPEG_GRID_FAILED_INPUTS_RETAINED":"JPEG_GRID_NATIVE_TABLES_NO_IDCT_NO_REQUANTIZE_COMPLETE");return rc;
}

/* Three coarse workers, then compressed scan concatenation. No pixel decode.
 * Each worker uses the original serial implementation. Scratch files are
 * confined to a newly-created private directory beside the output. */
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
static volatile sig_atomic_t interrupted;
static void request_stop(int sig){(void)sig;interrupted=1;}
static int partial_header(FILE *f,uint8_t *h,FjbHeader *info,unsigned *interval,off_t *length){
 uint8_t parse[4096];
 if(fseeko(f,0,SEEK_END)||(*length=ftello(f))<64||fseeko(f,-2,SEEK_END)||
    getc(f)!=255||getc(f)!=217||fseeko(f,0,SEEK_SET))return 0;
 size_t n=fread(h,1,4096,f);if(n<64)return 0;
 memset(parse,0,sizeof parse);memcpy(parse,h,n);parse[4094]=255;parse[4095]=217;
 unsigned seen=0;
 for(unsigned p=2;p+4<=n;){
  if(parse[p]!=255)return 0;unsigned marker=parse[p+1],len=u16(parse+p+2);
  if(len<2||p+2+len>n)return 0;
  if(marker==0xdd){if(seen++||len!=4)return 0;*interval=u16(parse+p+4);parse[p+4]=parse[p+5]=0;}
  if(marker==0xda)break;p+=2+len;
 }
 return seen==1&&*interval&&fjb_parse(parse,sizeof parse,info)&&info->scan<n&&fseeko(f,info->scan,SEEK_SET)==0;
}
static int copy_scan(FILE *in,FILE *out,off_t bytes,unsigned *rst,unsigned expected){
 uint8_t block[65536];int pending=0;unsigned count=0,local=0;
 while(bytes>0&&!interrupted){
  size_t n=bytes>(off_t)sizeof block?sizeof block:(size_t)bytes;
  if(fread(block,1,n,in)!=n)return 0;
  for(size_t i=0;i<n;i++){
   unsigned v=block[i];
   if(pending){
    pending=0;
    if(v==0)continue;
    if(v<208||v>215||v!=208+local%8)return 0;
    block[i]=(uint8_t)(208+(*rst)%8);(*rst)++;local++;count++;
   }else if(v==255)pending=1;
  }
  if(fwrite(block,1,n,out)!=n)return 0;bytes-=(off_t)n;
 }
 return !interrupted&&!pending&&count==expected;
}
int main(int argc,char **argv){
 if(argc!=7)return 2;
 unsigned v[4];for(unsigned i=0;i<4;i++){char *end;unsigned long n=strtoul(argv[3+i],&end,10);if(*end||!n||n>65535)return 2;v[i]=(unsigned)n;}
 unsigned width=v[0],height=v[1],sx=v[2],sy=v[3],cols=(width+sx-1)/sx,groups=(height+sy-1)/sy;
 /* Only the tested coarse layout uses parallelism; retain general serial fallback. */
 if(groups<2||groups>3)return serial_main(argc,argv);
 if(cols>16)return 2;
 char scratch[512],paths[3][560];pid_t children[3]={0};unsigned started=0;
 int rc=1,owned=0,failed=0;FILE *out=NULL,*in=NULL;
 int n=snprintf(scratch,sizeof scratch,"%s.parts-XXXXXX",argv[1]);if(n<1||n>=(int)sizeof scratch)return 2;
 struct stat existing;if(!lstat(argv[1],&existing)||errno!=ENOENT)return 1;
 if(!mkdtemp(scratch))return 1;
 struct sigaction action={0};action.sa_handler=request_stop;sigemptyset(&action.sa_mask);
 if(sigaction(SIGTERM,&action,NULL)||sigaction(SIGINT,&action,NULL))goto done;
 for(unsigned g=0;g<groups;g++){
  n=snprintf(paths[g],sizeof paths[g],"%s/part-%u.jpg",scratch,g);if(n<1||n>=(int)sizeof paths[g])goto done;
 }
 fflush(NULL);
 for(unsigned g=0;g<groups&&!interrupted;g++){
  pid_t parent=getpid(),p=fork();if(p<0){failed=1;break;}
  if(p==0){
   signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
#ifdef __linux__
   if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)_exit(3);
#else
   (void)parent;
#endif
   unsigned h=height-g*sy;if(h>sy)h=sy;char hs[24];snprintf(hs,sizeof hs,"%u",h);
   char *args[]={argv[0],paths[g],argv[2],argv[3],hs,argv[5],argv[6],NULL};tile_offset=g*cols;
   int child_rc=serial_main(7,args);fflush(NULL);_exit(child_rc);
  }
  children[g]=p;started++;
 }
 if(started!=groups)failed=1;
 for(unsigned g=0;g<started;g++){
  int status=0;pid_t got;
  if(interrupted||failed)for(unsigned j=g;j<started;j++)if(children[j]>0)kill(children[j],SIGTERM);
  do{got=waitpid(children[g],&status,0);if(got<0&&errno==EINTR&&interrupted)for(unsigned j=g;j<started;j++)if(children[j]>0)kill(children[j],SIGTERM);}while(got<0&&errno==EINTR);
  children[g]=0;if(got<0||!WIFEXITED(status)||WEXITSTATUS(status))failed=1;
 }
 if(failed||interrupted)goto done;
 int fd=open(argv[1],O_CREAT|O_EXCL|O_WRONLY|O_BINARY|O_NOFOLLOW,0600);if(fd<0)goto done;owned=1;
 out=fdopen(fd,"wb");if(!out){close(fd);goto done;}
 uint8_t base[4096]={0};FjbHeader base_info={0};unsigned base_interval=0,rst=0;
 for(unsigned g=0;g<groups;g++){
  uint8_t h[4096]={0};FjbHeader info;unsigned interval=0;off_t length;
  in=fopen(paths[g],"rb");if(!in||!partial_header(in,h,&info,&interval,&length))goto done;
  unsigned expected_h=height-g*sy;if(expected_h>sy)expected_h=sy;
  if(info.width!=width||info.height!=expected_h||sy%info.mcu_height||sx%info.mcu_width)goto done;
  if(!g){
   memcpy(base,h,info.scan);base_info=info;base_interval=interval;
   base[info.sof_height]=(uint8_t)(height>>8);base[info.sof_height+1]=(uint8_t)height;
   if(fwrite(base,1,info.scan,out)!=info.scan)goto done;
  }else{
   if(interval!=base_interval||!fjb_same_header(base,&base_info,h,&info))goto done;
   if(putc(255,out)<0||putc((int)(208+rst%8),out)<0)goto done;rst++;
  }
  unsigned mcus=((width+info.mcu_width-1)/info.mcu_width)*((expected_h+info.mcu_height-1)/info.mcu_height);
  if(g+1<groups&&mcus%interval)goto done;
  if(!copy_scan(in,out,length-info.scan-2,&rst,(mcus-1)/interval))goto done;
  if(fclose(in)){in=NULL;goto done;}in=NULL;
 }
 if(interrupted||putc(255,out)<0||putc(217,out)<0||fflush(out)||fsync(fileno(out)))goto done;
 rc=0;
done:
 if(in)fclose(in);
 if(out&&fclose(out))rc=1;
 if(rc&&owned)unlink(argv[1]);
 for(unsigned g=0;g<groups;g++){char p[560];int z=snprintf(p,sizeof p,"%s/part-%u.jpg",scratch,g);if(z>0&&z<(int)sizeof p)unlink(p);}
 rmdir(scratch);
 puts(rc?"JPEG_PARALLEL_FAILED_INPUTS_RETAINED":"JPEG_PARALLEL_NATIVE_TABLES_BYTE_COMPATIBLE_COMPLETE");return rc;
}
