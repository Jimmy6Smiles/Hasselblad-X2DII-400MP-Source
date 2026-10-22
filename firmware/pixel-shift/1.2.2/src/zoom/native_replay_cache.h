/* Private native-rendered JPEG binding. No photo names, calibration data or
 * volatile inode numbers are baked into a release. Never writes a RAW file. */
#ifndef NATIVE_REPLAY_CACHE_H
#define NATIVE_REPLAY_CACHE_H
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifndef NRC_MEDIA
#define NRC_MEDIA "/mnt/media_rw"
#endif
typedef struct {uint64_t size,mtime,header;} NrcIdentity;
typedef struct {char magic[16],job[24];NrcIdentity raw,jpeg;} NrcBinding;
static int nrc_digits(const char*s,size_t n){for(size_t i=0;i<n;i++)if(s[i]<'0'||s[i]>'9')return 0;return 1;}
static int nrc_item(const char*item){
 return item&&strlen(item)==22&&(!strncmp(item,"/cfe/",5)||!strncmp(item,"/ssd/",5))&&
 item[5]>='1'&&item[5]<='9'&&nrc_digits(item+5,3)&&!strncmp(item+8,"HASBL/B",7)&&nrc_digits(item+15,7);
}
static int nrc_job(const char*s){size_t n=strnlen(s,24);return n>0&&n<=18&&s[0]!='0'&&nrc_digits(s,n);}
static int nrc_identity(const char*p,NrcIdentity*out){
 int fd=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 struct stat a,b;unsigned char buf[65536];int ok=0;
 if(fstat(fd,&a)||!S_ISREG(a.st_mode)||a.st_size<4)goto done;
 size_t need=a.st_size<(off_t)sizeof buf?(size_t)a.st_size:sizeof buf;
 size_t at=0;while(at<need){ssize_t n=read(fd,buf+at,need-at);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;at+=(size_t)n;}
 uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<need;i++){h^=buf[i];h*=UINT64_C(1099511628211);}
 if(fstat(fd,&b)||a.st_size!=b.st_size||a.st_mtim.tv_sec!=b.st_mtim.tv_sec||a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec)goto done;
 *out=(NrcIdentity){(uint64_t)a.st_size,(uint64_t)a.st_mtim.tv_sec,h};ok=1;
done:close(fd);return ok;
}
static int nrc_paths(const char*item,char*raw,char*index,size_t cap){
 if(!nrc_item(item))return 0;char volume[4];memcpy(volume,item+1,3);volume[3]=0;
 int a=snprintf(raw,cap,NRC_MEDIA"/%s/DCIM/%s.3FR",volume,item+5);
 int b=snprintf(index,cap,NRC_MEDIA"/%s/.x2d2-pixelshift/replay/%.8s-%s.bind",volume,item+5,item+14);
 return a>0&&b>0&&(size_t)a<cap&&(size_t)b<cap;
}
static int nrc_read(const char*index,NrcBinding*b){
 int fd=open(index,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==sizeof *b&&read(fd,b,sizeof *b)==sizeof *b;
 close(fd);return ok&&!memcmp(b->magic,"X2D2REPLAYv1",12)&&nrc_job(b->job);
}
static int nrc_jpeg_path(const char*item,const NrcBinding*b,char*out,size_t cap){
 if(!nrc_item(item)||!nrc_job(b->job))return 0;
 int n=snprintf(out,cap,NRC_MEDIA"/%.3s/.x2d2-pixelshift/%s/native-full.jpg",item+1,b->job);
 return n>0&&(size_t)n<cap;
}
/* 0: no binding, -1: binding exists but fails validation, 1: valid cache. */
static int nrc_lookup(const char*item,char*jpeg,size_t cap){
 char raw[512],index[512];NrcBinding b;NrcIdentity r,j;struct stat st;
 if(!nrc_paths(item,raw,index,sizeof raw))return -1;
 if(lstat(index,&st))return errno==ENOENT?0:-1;
 if(!nrc_read(index,&b)||!nrc_jpeg_path(item,&b,jpeg,cap)||!nrc_identity(raw,&r)||
 !nrc_identity(jpeg,&j)||memcmp(&r,&b.raw,sizeof r)||memcmp(&j,&b.jpeg,sizeof j))return -1;
 return 1;
}
#endif
