/* 自写私有原型：本机回环 HTTP 区域提供器，单个显式源文件，有限寿命。
 * 不接受客户端文件路径，不绑定外网，不拍摄、不修改源图或系统。
 * scratch 必须为本次独占 RAM 目录；GUI 绑定与权限仍须单独验收。 */
#define main ps_region_cli_main
#include "zoom_region.c"
#undef main
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>

static int send_all(int fd,const void *data,size_t n){
 const char *p=data;
 while(n){ssize_t s=send(fd,p,n,MSG_NOSIGNAL);if(s<=0)return 0;p+=s;n-=(size_t)s;}return 1;
}
static void reject(int fd){const char reply[]="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,reply,sizeof reply-1);}
static int parse_region(const char *req,unsigned *x,unsigned *y,unsigned *w,unsigned *h){
 const char prefix[]="GET /region/";unsigned *values[]={x,y,w,h};
 if(strncmp(req,prefix,sizeof prefix-1))return 0;
 req+=sizeof prefix-1;
 for(unsigned i=0;i<4;i++){
  char text[6];unsigned count=0;
  while(*req>='0'&&*req<='9'){if(count==5)return 0;text[count++]=*req++;}
  text[count]=0;if(!integer(text,values[i]))return 0;
  if(i<3){if(*req++!='/')return 0;}
 }
 return !strncmp(req," HTTP/1.1\r\n",11);
}
#ifndef PS_ZOOM_SERVER_LIBRARY
int main(int argc,char **argv){
 if(argc!=5)return 2;
 unsigned port,ttl;if(!integer(argv[3],&port)||port<1024||!integer(argv[4],&ttl)||!ttl||ttl>600)return 2;
 if(argv[1][0]!='/'||argv[2][0]!='/'||strlen(argv[2])>3900)return 2;
 struct stat st;if(lstat(argv[2],&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 2;
 char dest[4096],part[4096];snprintf(dest,sizeof dest,"%s/response.ppm",argv[2]);snprintf(part,sizeof part,"%s/response.ppm.partial",argv[2]);
 if(!lstat(dest,&st)||errno!=ENOENT||!lstat(part,&st)||errno!=ENOENT)return 2;
 int listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)return 1;
 struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((uint16_t)port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(bind(listener,(struct sockaddr*)&addr,sizeof addr)||listen(listener,4)){close(listener);return 1;}
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
 struct timespec start,current;clock_gettime(CLOCK_MONOTONIC,&start);
 puts("ZOOM_LOOPBACK_READY");fflush(stdout);
 while(!cancelled){
  clock_gettime(CLOCK_MONOTONIC,&current);if(current.tv_sec-start.tv_sec>=ttl)break;
  struct pollfd pollfd={listener,POLLIN,0};int ready=poll(&pollfd,1,100);if(ready<0&&errno!=EINTR)break;if(ready<=0)continue;
  int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC);if(fd<0)continue;
  struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
  char req[512];size_t used=0;int complete=0;
  while(used<sizeof req-1){ssize_t n=recv(fd,req+used,sizeof req-1-used,0);if(n<=0)break;used+=(size_t)n;req[used]=0;if(strstr(req,"\r\n\r\n")){complete=1;break;}}
  unsigned x,y,w,h;
  if(!complete||!parse_region(req,&x,&y,&w,&h)||
     !w||!h||w>1024||h>1024||x>=23310||y>=17482||w>23310-x||h>17482-y){reject(fd);close(fd);continue;}
  if(region(argv[1],dest,x,y,w,h)){reject(fd);close(fd);continue;}
  FILE *file=fopen(dest,"rb");
  if(!file||fstat(fileno(file),&st)||st.st_size>3145800){if(file)fclose(file);unlink(dest);reject(fd);close(fd);continue;}
  char header[256];int n=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: image/x-portable-pixmap\r\nContent-Length: %lld\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",(long long)st.st_size);
  int ok=send_all(fd,header,(size_t)n);char block[8192];size_t got;
  while(ok&&(got=fread(block,1,sizeof block,file)))ok=send_all(fd,block,got);
  fclose(file);unlink(dest);close(fd);
 }
 close(listener);return 0;
}
#endif
