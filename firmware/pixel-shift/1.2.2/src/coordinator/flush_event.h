/* Observation data only. NOT a trusted READY event or permission to read/delete. */
#ifndef PS_FLUSH_EVENT_H
#define PS_FLUSH_EVENT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {void *owner;const uint16_t *chars;int64_t length;} FlushQString;
typedef struct {uint64_t monotonic_ns;char path[64];int canonical;int source;uint32_t work_kind;unsigned error_code;} FlushEvent;
/* Exact-firmware observation only. Result contains a shared Work at +0;
 * Work kind +8, Work QString +0x20; Result error byte +0x20.
 * Called only for FileIO::itemProcessed(Result const&), never for the narrower
 * shared-Work callback. No pointer or shared ownership escapes the signal. */
static inline const FlushQString *flush_result_view(const void *result,FlushEvent *e){
    if(!result)return NULL;
    const unsigned char *r=result;const unsigned char *work=NULL;
    memcpy(&work,r,sizeof work);e->error_code=r[0x20];
    if(!work)return NULL;
    memcpy(&e->work_kind,work+8,sizeof e->work_kind);
    return (const FlushQString*)(work+0x20);
}
static int flush_path(const FlushQString *s,FlushEvent *e){
    if(!s||!s->chars||s->length<=0||s->length>=64)return 0;
    char p[64];
    for(int64_t i=0;i<s->length;i++){if(s->chars[i]>127)return 0;p[i]=(char)s->chars[i];}
    p[s->length]=0;
    const char *prefix="/mnt/media_rw/";size_t n=strlen(prefix);
    if(strncmp(p,prefix,n))return 0;
    if(strncmp(p+n,"cfe/DCIM/",9)&&strncmp(p+n,"ssd/DCIM/",9))return 0;
    n+=9;
    if(strlen(p+n)!=21||p[n]<'1'||p[n]>'9')return 0;
    for(unsigned i=1;i<3;i++)if(p[n+i]<'0'||p[n+i]>'9')return 0;
    if(memcmp(p+n+3,"HASBL/B",7))return 0;
    for(unsigned i=10;i<17;i++)if(p[n+i]<'0'||p[n+i]>'9')return 0;
    if(strcmp(p+n+17,".3FR"))return 0;
    memcpy(e->path,p,(size_t)s->length+1);return 1;
}
#endif
