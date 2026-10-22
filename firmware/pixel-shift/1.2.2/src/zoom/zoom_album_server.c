/* 自写相册级回放：只识别本项目标记且通过 RAW 解析的合成文件。
 * 请求仅允许 /ssd|cfe/NNNHASBL/BNNNNNNN；不允许任意路径，不写照片。
 * 文件代次绑定区域 URL，换图、换卡和替换文件不能沿用旧响应。 */
#define PS_ZOOM_SERVER_LIBRARY
#include "zoom_server.c"
#undef main
#include <limits.h>
#include "jpeg_zoom_region.h"
static int album_item(const char *s){
 if(strlen(s)!=22 || s[0]!='/' || (strncmp(s+1,"ssd/",4)&&strncmp(s+1,"cfe/",4)))return 0;
 if(s[5]<'1'||s[5]>'9'||s[6]<'0'||s[6]>'9'||s[7]<'0'||s[7]>'9'||strncmp(s+8,"HASBL/B",7))return 0;
 for(unsigned i=15;i<22;i++)if(s[i]<'0'||s[i]>'9')return 0;
 return s[22]==0;
}
static void reply(int fd,const char *kind,const void *body,size_t bytes){
 char header[256];int n=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",kind,bytes);
 if(n>0&&(size_t)n<sizeof header&&send_all(fd,header,(size_t)n))send_all(fd,body,bytes);
}
static void identity(const struct stat *s,char text[17]){
 uint64_t fields[]={(uint64_t)s->st_dev,(uint64_t)s->st_ino,(uint64_t)s->st_size,(uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,(uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};
 uint64_t h=UINT64_C(14695981039346656037);
 const unsigned char *p=(const unsigned char*)fields;
 for(size_t i=0;i<sizeof fields;i++)h=(h^p[i])*UINT64_C(1099511628211);
 snprintf(text,17,"%016llx",(unsigned long long)h);
}
static int source_info(const char *path,struct stat *st){
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 FILE *f=fdopen(fd,"rb");if(!f){close(fd);return 0;}
 FmIfd root={0};unsigned char head[8];const unsigned tags[]={305};int ok=0;
 if(fstat(fd,st)||!S_ISREG(st->st_mode)||st->st_size<8||!fm_read(f,st->st_size,0,head,8)||memcmp(head,"II*\0",4))goto done;
 unsigned off=fm32(head+4);if(off>=FM_LIMIT||!fm_parse(f,st->st_size,off,&root,tags,1))goto done;
 FmTag *tag=fm_find(&root,305);const char expected[]="X2DII experimental merge + matrix preview";
 ok=tag&&tag->type==2&&tag->count==sizeof expected&&!memcmp(tag->data,expected,sizeof expected);
done:fm_free(&root);fclose(f);return ok;
}
int main(int argc,char **argv){
 /* 两个 DCIM 根目录由可信启动器提供；不是从网络传入。 */
 if(argc!=6)return 2;
 unsigned port,ttl=0;int resident=!strcmp(argv[5],"resident");
 if(!integer(argv[4],&port)||port<1024||(!resident&&(!integer(argv[5],&ttl)||!ttl||ttl>3600)))return 2;
 for(int i=1;i<=3;i++)if(argv[i][0]!='/'||strlen(argv[i])>3000)return 2;
 struct stat st;if(lstat(argv[3],&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 2;
 char dest[4096],part[4096];snprintf(dest,sizeof dest,"%s/response.ppm",argv[3]);snprintf(part,sizeof part,"%s/response.ppm.partial",argv[3]);
 if(!lstat(dest,&st)||errno!=ENOENT||!lstat(part,&st)||errno!=ENOENT)return 2;
 int listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)return 1;
 int reuse=1;if(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)){close(listener);return 1;}
 struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((uint16_t)port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(bind(listener,(struct sockaddr*)&addr,sizeof addr)||listen(listener,4)){close(listener);return 1;}
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
 struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);puts("ALBUM_ZOOM_READY");fflush(stdout);
 while(!cancelled){
  clock_gettime(CLOCK_MONOTONIC,&now);if(!resident&&now.tv_sec-start.tv_sec>=ttl)break;
  struct pollfd p={listener,POLLIN,0};if(poll(&p,1,100)<=0)continue;
  int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC);if(fd<0)continue;
  struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
  char req[512]={0},url[160]={0};size_t used=0;
  while(used<sizeof req-1&&!strstr(req,"\r\n\r\n")){ssize_t n=recv(fd,req+used,sizeof req-1-used,0);if(n<=0)break;used+=(size_t)n;req[used]=0;}
  if(!strstr(req,"\r\n\r\n")||strncmp(req,"GET ",4)){reject(fd);close(fd);continue;}
  char *end=strchr(req+4,' ');size_t len=end?(size_t)(end-req-4):0;
  if(!len||len>=sizeof url||strncmp(end," HTTP/1.1\r\n",11)){reject(fd);close(fd);continue;}
  memcpy(url,req+4,len);int info=!strncmp(url,"/info/",6),roi=!strncmp(url,"/region/",8);
  char item[23]={0},path[4096],canonical[PATH_MAX],token[17];const char *tail=url+(info?5:7);
  if((!info&&!roi)||strlen(tail)<22){reject(fd);close(fd);continue;}
  memcpy(item,tail,22);
  if(!album_item(item)||(info&&tail[22])){reject(fd);close(fd);continue;}
  snprintf(path,sizeof path,"%s/%s.3FR",item[1]=='s'?argv[1]:argv[2],item+5);
  if(!realpath(path,canonical)||strcmp(path,canonical)||!source_info(path,&st)){reject(fd);close(fd);continue;}
  int paired=0;char jpg[4096];struct stat js;
  int pn=snprintf(jpg,sizeof jpg,"%s/%s.JPG",item[1]=='s'?argv[1]:argv[2],item+5);
  if(pn>0&&(size_t)pn<sizeof jpg&&realpath(jpg,canonical)&&!strcmp(jpg,canonical)&&pair_header(jpg,&js)){strcpy(path,jpg);st=js;paired=1;}
  identity(&st,token);
  unsigned x=0,y=0,w=1,h=1;
  if(roi){
   if(tail[22]!='/'||strlen(tail+23)<18||strncmp(tail+23,token,16)||tail[39]!='/'){reject(fd);close(fd);continue;}
   char region_request[128];snprintf(region_request,sizeof region_request,"GET /region/%s HTTP/1.1\r\n",tail+40);
   if(!parse_region(region_request,&x,&y,&w,&h)){reject(fd);close(fd);continue;}
  }
  if(!w||!h||w>23310||h>17482||x>=23310||y>=17482||w>23310-x||h>17482-y||(paired?(info?0:pair_region(path,dest,x,y,w,h,&st)):region_checked(path,dest,x,y,w,h,&st))){reject(fd);close(fd);continue;}
  struct stat after;if(lstat(path,&after)||!same_file(&st,&after)){unlink(dest);reject(fd);close(fd);continue;}
  if(info){
   char body[256];int n=snprintf(body,sizeof body,"{\"item\":\"%s\",\"token\":\"%s\",\"width\":23310,\"height\":17482}",item,token);
   reply(fd,"application/json",body,(size_t)n);
  }else{
   FILE *f=fopen(dest,"rb");unsigned char *body=NULL;struct stat output;
   if(f&&!fstat(fileno(f),&output)&&output.st_size>0&&output.st_size<=3145800){body=malloc((size_t)output.st_size);if(body&&fread(body,1,(size_t)output.st_size,f)==(size_t)output.st_size)reply(fd,"image/x-portable-pixmap",body,(size_t)output.st_size);else reject(fd);}
   else reject(fd);
   free(body);if(f)fclose(f);
  }
  printf("ALBUM_ZOOM_%s item=%s source=%s x=%u y=%u w=%u h=%u\n",info?"INFO":"REGION",item,paired?"JPEG":"RAW",x,y,w,h);fflush(stdout);
  unlink(dest);close(fd);
 }
 close(listener);return 0;
}
