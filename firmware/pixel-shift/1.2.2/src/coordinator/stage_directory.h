/* Prepare only the private staging directory on the already verified volume.
 * Pin directories, reject links and existing job names; never remove content. */
static int ps_create_stage(const char *volume,const char *id){
 if(!id||!*id||strlen(id)>18||*id=='0')return 0;
 for(const char *p=id;*p;p++)if(*p<'0'||*p>'9')return 0;
 int v=open(volume,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(v<0)return 0;
 int d=-1,ok=0;struct stat vs,ds;
 if(fstat(v,&vs))goto out;
 if(mkdirat(v,".x2d2-pixelshift",0700)&&errno!=EEXIST)goto out;
 d=openat(v,".x2d2-pixelshift",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(d<0||fstat(d,&ds)||ds.st_dev!=vs.st_dev)goto out;
 if(mkdirat(d,id,0700)||fsync(d)||fsync(v))goto out;
 ok=1;
out:
 if(d>=0)close(d);
 close(v);return ok;
}
