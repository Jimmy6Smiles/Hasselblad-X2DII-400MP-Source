#define _GNU_SOURCE
#define NRC_MEDIA "."
#define main publish_main
#include "../src/native-cache/native_replay_publish.c"
#undef main
#include <assert.h>
static void makefile(const char*p,off_t size){int fd=open(p,O_CREAT|O_EXCL|O_RDWR,0600);assert(fd>=0);assert(write(fd,"TEST",4)==4);assert(!ftruncate(fd,size));assert(!close(fd));}
int main(void){
 char root[]="/tmp/x2d2-replay-test-XXXXXX";assert(mkdtemp(root));assert(!chdir(root));
 const char*dirs[]={"cfe","cfe/DCIM","cfe/DCIM/999HASBL","cfe/.x2d2-pixelshift","cfe/.x2d2-pixelshift/123"};
 for(unsigned i=0;i<5;i++)assert(!mkdir(dirs[i],0700));
 const char*raw="cfe/DCIM/999HASBL/B0000001.3FR",*jpeg="cfe/.x2d2-pixelshift/123/native-full.jpg";
 makefile(raw,815010841);makefile(jpeg,10001);makefile("cfe/.x2d2-pixelshift/123/keep.dat",8);
 makefile("cfe/.x2d2-pixelshift/123/render-tile-0.jpg",8);
 char*args[]={"cache","--publish","/cfe/999HASBL/B0000001","123",NULL};char path[512];
 assert(nrc_lookup(args[2],path,sizeof path)==0);assert(publish_main(4,args)==0);
 assert(nrc_lookup(args[2],path,sizeof path)==1);assert(access("cfe/.x2d2-pixelshift/123/render-tile-0.jpg",F_OK));
 assert(!access("cfe/.x2d2-pixelshift/123/keep.dat",F_OK));
 assert(publish_main(4,args)!=0); /* never overwrite an existing binding */
 assert(!nrc_item("/cfe/../HASBL/B0000001"));assert(!nrc_job("../123"));
 struct stat old;assert(!stat(raw,&old));assert(!rename(raw,"raw.old"));makefile(raw,815010841);
 struct timespec times[2]={old.st_atim,old.st_mtim};assert(!utimensat(AT_FDCWD,raw,times,0));
 assert(nrc_lookup(args[2],path,sizeof path)==1); /* inode changes across mount */
 int fd=open(raw,O_WRONLY);assert(fd>=0&&write(fd,"FAIL",4)==4);close(fd);
 assert(nrc_lookup(args[2],path,sizeof path)==-1);
 assert(prune("cfe"));assert(nrc_lookup(args[2],path,sizeof path)==0);assert(access(jpeg,F_OK));
 assert(!access(raw,F_OK)&&!access("raw.old",F_OK)&&!access("cfe/.x2d2-pixelshift/123/keep.dat",F_OK));
 assert(!unlink(raw)&&!unlink("raw.old")&&!unlink("cfe/.x2d2-pixelshift/123/keep.dat"));
 assert(!rmdir("cfe/.x2d2-pixelshift/replay"));for(int i=4;i>=0;i--)assert(!rmdir(dirs[i]));assert(!chdir("/"));assert(!rmdir(root));
 puts("REPLAY_BINDING_NO_CLOBBER_REBOOT_IDENTITY_STALE_CACHE_PRUNE_PASS");return 0;
}
