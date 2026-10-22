#define PS_JOB_TEST
#include "integrated_job.c"
#include <assert.h>
static void file(const char *p){int f=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);assert(f>=0);assert(write(f,"abcd",4)==4);assert(!close(f));}
int main(void){
 char dir[]="/var/tmp/identity-guard-XXXXXX",a[65],b[65];assert(mkdtemp(dir));assert(!chdir(dir));
 file("first");assert(source_identity("first",a));assert(source_identity("first",b)&&!strcmp(a,b));
 assert(!rename("first","renamed"));assert(identity_after_rename("first","renamed"));assert(source_identity("renamed",b)&&!strcmp(a,b));
 struct stat before;assert(!stat("renamed",&before));int fd=open("renamed",O_WRONLY);assert(fd>=0);assert(write(fd,"wxyz",4)==4);close(fd);
 struct timespec times[2]={before.st_atim,before.st_mtim};assert(!utimensat(AT_FDCWD,"renamed",times,0));
 assert(!source_identity("renamed",b)); /* ctime catches restored mtime */
 identity_reset();file("replace");assert(source_identity("replace",a));assert(!unlink("replace"));file("replace");assert(!source_identity("replace",b));
 identity_reset();file("original");assert(!symlink("original","link"));assert(!source_identity("link",a));
 assert(!link("original","hard"));assert(!source_identity("original",a));assert(!unlink("hard"));
 assert(source_identity("original",a));assert(!rename("original","moved"));
 assert(truncate("moved",2)==0);assert(!identity_after_rename("original","moved"));
 identity_reset();for(int i=0;i<8;i++){char n[32];snprintf(n,sizeof n,"item%d",i);file(n);assert(source_identity(n,a));}
 file("overflow");assert(!source_identity("overflow",a));identity_reset();
 puts("IDENTITY_GUARD_PASS rename mutation restored-mtime replacement symlink hardlink capacity");return 0;
}
