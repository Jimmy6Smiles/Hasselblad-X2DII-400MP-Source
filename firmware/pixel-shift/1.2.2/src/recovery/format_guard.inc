/* 自写保存格式保护，供 native_auto6.c 复用固定 odindb 调用。
 * 原值写日志后才借用 RAW；只在空闲且当前仍为本模块 RAW/原值时恢复。
 * 与当前 AE 日志一样，当前实验版只允许同次开机恢复，不是断电恢复承诺。 */
typedef struct {char magic[8],boot[40];uint32_t original,phase,check;} PsFormatRecord;
_Static_assert(sizeof(PsFormatRecord)==60,"format journal ABI");
static const char *ps_formats[]={"E_ImageFormat_Raw","E_ImageFormat_RawJpeg","E_ImageFormat_Jpeg","E_ImageFormat_RawHeif","E_ImageFormat_Heif"};
static uint32_t fmt_hash(const PsFormatRecord *r){
 const unsigned char *p=(const unsigned char*)r;uint32_t h=2166136261u;
 for(size_t i=0;i<sizeof(*r)-4;i++)h=(h^p[i])*16777619u;
 return h;
}
static int fmt_get(uint32_t *v){
 char b[LIMIT],expected[100];if(!call("image_format",NULL,b))return 0;
 for(unsigned i=0;i<5;i++){snprintf(expected,sizeof expected,"image_format = %s(%u)\n",ps_formats[i],i);
  if(!strcmp(b,expected)){*v=i;return 1;}}
 return 0;
}
static int fmt_set(uint32_t v){
 char b[LIMIT];uint32_t actual;
 return v<5&&call("image_format",ps_formats[v],b)&&!b[0]&&fmt_get(&actual)&&actual==v;
}
static int fmt_save(int fd,PsFormatRecord *r){
 r->check=fmt_hash(r);return pwrite(fd,r,sizeof *r,0)==sizeof *r&&!ftruncate(fd,sizeof *r)&&!fsync(fd);
}
static int fmt_restore(int fd,PsFormatRecord *r){
 uint32_t v;if(!idle()||!fmt_get(&v)||(v!=0&&v!=r->original))return 0;
 if(v!=r->original&&!fmt_set(r->original))return 0;
 if(!fmt_get(&v)||v!=r->original)return 0;
 r->phase=2;return fmt_save(fd,r);
}
static int fmt_open(const char *path,PsFormatRecord *r,int recovery){
 int fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|(recovery?0:O_CREAT|O_EXCL),0600);struct stat st;
 if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}
 if(recovery){char boot[40]={0};
  if(st.st_size!=sizeof *r||pread(fd,r,sizeof *r,0)!=sizeof *r||memcmp(r->magic,"X2DFMT1",8)||
   r->original>4||r->phase<1||r->phase>2||r->check!=fmt_hash(r)||!boot_id(boot)||memcmp(boot,r->boot,40)){close(fd);return -1;}
 }else{
  memset(r,0,sizeof *r);memcpy(r->magic,"X2DFMT1",8);r->phase=1;
  if(!boot_id(r->boot)||!idle()||!fmt_get(&r->original)||!fmt_save(fd,r)||!parent_sync(path)){close(fd);return -1;}
 }
 return fd;
}
