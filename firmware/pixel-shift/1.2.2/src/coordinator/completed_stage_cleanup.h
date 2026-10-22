/* Called only after verified publication, album registration and six-input
 * cleanup. Never recurse, follow symlinks, or remove anything except this
 * job's merged.dng whose identity was captured after reconstruction. */
static int completed_stage_cleanup(const char *stage,const struct stat *expected){
 int d=open(stage,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(d<0)return 0;
 struct stat parent,now;int ok=0;
 if(fstat(d,&parent)||parent.st_dev!=expected->st_dev||
    fstatat(d,"merged.dng",&now,AT_SYMLINK_NOFOLLOW)||
    !S_ISREG(now.st_mode)||now.st_nlink!=1||
    now.st_dev!=expected->st_dev||now.st_ino!=expected->st_ino||
    now.st_size!=expected->st_size||
    now.st_mtim.tv_sec!=expected->st_mtim.tv_sec||
    now.st_mtim.tv_nsec!=expected->st_mtim.tv_nsec||
    now.st_ctim.tv_sec!=expected->st_ctim.tv_sec||
    now.st_ctim.tv_nsec!=expected->st_ctim.tv_nsec)goto end;
 if(unlinkat(d,"merged.dng",0)||fsync(d))goto end;
 ok=1;
end:
 close(d);return ok;
}
