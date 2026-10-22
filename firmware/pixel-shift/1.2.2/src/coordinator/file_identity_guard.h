/* Per-job descriptor-pinned identity tokens, NOT content digests.
 * The merge worker alone performs complete output readback. No pixel reads here.
 * Tokens are valid only in this process; recovery must never reuse them. */
#ifndef PS_FILE_IDENTITY_GUARD_H
#define PS_FILE_IDENTITY_GUARD_H
typedef struct {int used,fd;struct stat st;char path[512];} PsFileIdentity;
static PsFileIdentity ps_identities[8];
static int identity_equal(const struct stat *a,const struct stat *b,int ctime){
 return S_ISREG(b->st_mode)&&b->st_nlink==1&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
 a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 (!ctime||(a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec));
}
static void identity_reset(void){
 for(unsigned i=0;i<8;i++)if(ps_identities[i].used)close(ps_identities[i].fd);
 memset(ps_identities,0,sizeof ps_identities);
}
static int source_identity(const char *path,char token[65]){
 struct stat s,pinned;int vacant=-1;
 if(strlen(path)>=sizeof ps_identities[0].path||!regular(path,&s))return 0;
 for(unsigned i=0;i<8;i++){
  PsFileIdentity *r=&ps_identities[i];if(!r->used){if(vacant<0)vacant=(int)i;continue;}
  if(!strcmp(path,r->path)){
   if(!identity_equal(&r->st,&s,1)||fstat(r->fd,&pinned)||!identity_equal(&r->st,&pinned,1))return 0;
   snprintf(token,65,"%064x",i+1);return 1;
  }
 }
 if(vacant<0)return 0;
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 if(fstat(fd,&pinned)||!identity_equal(&s,&pinned,1)){close(fd);return 0;}
 PsFileIdentity *r=&ps_identities[vacant];r->used=1;r->fd=fd;r->st=pinned;strcpy(r->path,path);
 snprintf(token,65,"%064x",vacant+1);return 1;
}
/* Only immediately after our successful no-overwrite rename. Rename changes
 * ctime legitimately; require the pinned inode, size and mtime to be unchanged. */
static int identity_after_rename(const char *from,const char *to){
 if(strlen(to)>=sizeof ps_identities[0].path)return 0;
 struct stat s,pinned;if(!regular(to,&s))return 0;
 for(unsigned i=0;i<8;i++){
  PsFileIdentity *r=&ps_identities[i];if(!r->used||strcmp(r->path,from))continue;
  if(!identity_equal(&r->st,&s,0)||fstat(r->fd,&pinned)||!identity_equal(&s,&pinned,1))return 0;
  r->st=s;strcpy(r->path,to);return 1;
 }
 return 0;
}
#endif
