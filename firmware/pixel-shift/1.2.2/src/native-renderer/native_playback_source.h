/* Read-only published-RAW binding for the native playback adapter.
 * No six-frame inputs, JPEG fallback, calibration, or camera API calls here.
 * Requests carry the source generation and a monotonically increasing ticket;
 * only the latest ticket may publish. Hardware buffer retirement is separate.
 */
#ifndef NATIVE_PLAYBACK_SOURCE_H
#define NATIVE_PLAYBACK_SOURCE_H
#include "native_cache_rows.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
typedef struct {
 NativeCacheRows raw;
 char path[256], generation[17];
 uint64_t latest;
} NativePlaybackSource;
typedef struct {uint64_t ticket;unsigned x,y,w,h;} NativePlaybackRequest;
static int nps_same(const struct stat*a,const struct stat*b){
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void nps_token(const struct stat*s,char out[17]){
 uint64_t fields[]={(uint64_t)s->st_dev,(uint64_t)s->st_ino,(uint64_t)s->st_size,
 (uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,
 (uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};
 uint64_t hash=UINT64_C(14695981039346656037);
 const unsigned char*p=(const unsigned char*)fields;
 for(size_t i=0;i<sizeof fields;i++)hash=(hash^p[i])*UINT64_C(1099511628211);
 snprintf(out,17,"%016llx",(unsigned long long)hash);
}
static int nps_path(const char*p){
 if(!p)return 0;
 const char*private_roots[]={"/mnt/media_rw/cfe/.x2d2-pixelshift/","/mnt/media_rw/ssd/.x2d2-pixelshift/"};
 for(unsigned i=0;i<2;i++)if(!strncmp(p,private_roots[i],strlen(private_roots[i]))){
  const char*t=p+strlen(private_roots[i]),*q=t;
  if(*q<'1'||*q>'9')return 0;
  while(*q>='0'&&*q<='9')q++;
  return q-t<=18&&!strcmp(q,"/output.3FR");
 }
 const char*c="/mnt/media_rw/cfe/DCIM/",*s="/mnt/media_rw/ssd/DCIM/";
 if(!strncmp(p,c,strlen(c)))p+=strlen(c);
 else if(!strncmp(p,s,strlen(s)))p+=strlen(s);else return 0;
 if(strlen(p)!=21||p[0]<'1'||p[0]>'9'||strncmp(p+3,"HASBL/B",7)||strcmp(p+17,".3FR"))return 0;
 for(unsigned i=1;i<3;i++)if(p[i]<'0'||p[i]>'9')return 0;
 for(unsigned i=10;i<17;i++)if(p[i]<'0'||p[i]>'9')return 0;
 return 1;
}
static int nps_current(const NativePlaybackSource*s){
 struct stat fd,named;char canonical[PATH_MAX];
 return s&&s->raw.fd>=0&&realpath(s->path,canonical)&&!strcmp(canonical,s->path)&&
 !fstat(s->raw.fd,&fd)&&!lstat(s->path,&named)&&
 nps_same(&fd,&s->raw.identity)&&nps_same(&named,&s->raw.identity);
}
static int nps_open(NativePlaybackSource*s,const char*p,const volatile sig_atomic_t*cancel){
 if(!s)return 0;memset(s,0,sizeof *s);s->raw.fd=-1;
 if(!nps_path(p)||strlen(p)>=sizeof s->path)return 0;
 strcpy(s->path,p);
 if(!ncr_open_container(&s->raw,p,cancel))return 0;
 if(!nps_current(s)){ncr_close(&s->raw);return 0;}
 nps_token(&s->raw.identity,s->generation);return 1;
}
static int nps_request(NativePlaybackSource*s,const char*generation,
 unsigned x,unsigned y,unsigned w,unsigned h,NativePlaybackRequest*r){
 if(!s||!r||!generation||strcmp(generation,s->generation)||!nps_current(s)||
 s->latest==UINT64_MAX||!w||!h||x>=23310||y>=17482||w>23310-x||h>17482-y)return 0;
 *r=(NativePlaybackRequest){++s->latest,x,y,w,h};return 1;
}
static int nps_publishable(const NativePlaybackSource*s,const NativePlaybackRequest*r){
 return s&&r&&r->ticket&&r->ticket==s->latest&&
 !(s->raw.cancel&&*s->raw.cancel)&&nps_current(s);
}
static void nps_close(NativePlaybackSource*s){if(s){ncr_close(&s->raw);s->latest=0;s->generation[0]=0;}}
#endif
