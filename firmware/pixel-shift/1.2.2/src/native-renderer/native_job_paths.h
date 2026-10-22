/* Validate a capture job's own RAM and staging directories. No fixed photo,
 * folder, storage device or experiment nonce may leak into a new capture. */
#ifndef NATIVE_JOB_PATHS_H
#define NATIVE_JOB_PATHS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {char work[128],stage[160],cache[192],preview[160],first[256];uint64_t nonce;} NativeJobPaths;
static inline int njp_init(NativeJobPaths*p,const char*work,const char*stage,uint64_t nonce){
 const char*prefix="/dev/x2d2-integrated-v1/jobs/";
 if(!p||!work||!stage||!nonce||strncmp(work,prefix,strlen(prefix)))return 0;
 const char*id=work+strlen(prefix);size_t n=strlen(id);
 if(!n||n>18||id[0]=='0')return 0;
 for(size_t i=0;i<n;i++)if(id[i]<'0'||id[i]>'9')return 0;
 char a[160],b[160];
 snprintf(a,sizeof a,"/mnt/media_rw/cfe/.x2d2-pixelshift/%s",id);
 snprintf(b,sizeof b,"/mnt/media_rw/ssd/.x2d2-pixelshift/%s",id);
 if(strcmp(stage,a)&&strcmp(stage,b))return 0;
 *p=(NativeJobPaths){.nonce=nonce};strcpy(p->work,work);strcpy(p->stage,stage);
 snprintf(p->cache,sizeof p->cache,"%s/native.u16.partial",stage);
 snprintf(p->preview,sizeof p->preview,"%s/native-preview.jpg",work);return 1;
}
#endif
