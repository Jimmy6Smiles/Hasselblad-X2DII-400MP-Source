/* Explicit per-output request, executed only on the storage QObject thread.
 * Native setters update allocator, signal and persistent index together.
 * No file deletion, exposure, reset-image-sequence or folder creation. */
#include "dcf_sync_contract.h"
#ifndef PS_DCF_HOST_TEST
#include "dcf_fingerprint.h"
#endif
static int dcf_qequals(const PsQStringView *q,const char *s){
 size_t n=strlen(s);if(!q||!q->chars||q->length!=(int64_t)n)return 0;
 for(size_t i=0;i<n;i++)if(q->chars[i]!=(unsigned char)s[i])return 0;return 1;
}
static int dcf_object(void *p,uintptr_t vt,void *thread){
 return p&&*(uintptr_t*)p==base+vt&&album_object_thread(p)==thread;
}
static void dcf_before(void *self,const void *path,int options){
 char folder[14];if(options||!album_path(path,folder))return;
 PsDcfSync r;struct stat st;int fd=open(PS_ALBUM_REFRESH_ROOT"/dcf.request",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0)return;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==sizeof r&&read(fd,&r,sizeof r)==sizeof r;close(fd);
 struct timespec ts;if(!ok||clock_gettime(CLOCK_MONOTONIC,&ts)||!ps_dcf_valid(&r,folder,(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000))return;
 for(unsigned i=0;i<sizeof dcf_checks/sizeof *dcf_checks;i++)if(memcmp((void*)(base+dcf_checks[i].offset),dcf_checks[i].bytes,dcf_checks[i].length))return;
 void *thread=album_current_thread();if(!thread||!dcf_object(self,PS_ALBUM_SERVICE_VTABLE,thread))return;
 if(((int(*)(void*))(base+DCF_PROCESSING))(self)!=0)return;
 void *main=*(void**)((char*)self+0xf0);if(!dcf_object(main,DCF_MAIN_VT,thread))return;
 void *dcf=*(void**)((char*)main+0x20),*index=*(void**)((char*)main+0x40);
 if(!dcf_object(dcf,DCF_VT,thread)||!dcf_object(index,DCF_INDEX_VT,thread))return;
 char media[32];snprintf(media,sizeof media,"/mnt/media_rw/%.3s",folder+1);
 void *volume=NULL;
 for(unsigned off=0x40;off<=0x48;off+=8){
  void *v=*(void**)((char*)dcf+off);if(!dcf_object(v,DCF_VOLUME_VT,thread))return;
  const PsQStringView *p=((const PsQStringView*(*)(void*))(base+DCF_VOLUME_PATH))(v);
  if(dcf_qequals(p,media)){if(volume)return;volume=v;}
 }
 int folder_index=(folder[5]-'0')*100+(folder[6]-'0')*10+folder[7]-'0';
 if(!volume||((int(*)(void*))(base+DCF_VOLUME_FOLDER))(volume)!=folder_index)return;
 int global=((int(*)(void*))(base+DCF_MAIN_INDEX))(main);
 int next=((int(*)(void*))(base+DCF_VOLUME_INDEX))(volume)+1;
 int persistent=((int(*)(void*))(base+DCF_INDEX_GET))(index);
 if(!ps_dcf_indices(&r,global,next,persistent))return;
 char file[96];snprintf(file,sizeof file,"%s/DCIM/%.8s/B%07u.3FR",media,folder+5,r.target);
 struct stat photo;if(lstat(file,&photo)||!S_ISREG(photo.st_mode)||photo.st_nlink!=1||
  (uint64_t)photo.st_dev!=r.device||(uint64_t)photo.st_ino!=r.inode||(uint64_t)photo.st_size!=r.bytes)return;
 char claim[sizeof(PS_ALBUM_REFRESH_ROOT)+64],result[sizeof(PS_ALBUM_REFRESH_ROOT)+64];
 snprintf(claim,sizeof claim,PS_ALBUM_REFRESH_ROOT"/dcf-%016llx.claimed",(unsigned long long)r.token);
 snprintf(result,sizeof result,PS_ALBUM_REFRESH_ROOT"/dcf-%016llx.result",(unsigned long long)r.token);
 if(syscall(SYS_renameat2,AT_FDCWD,PS_ALBUM_REFRESH_ROOT"/dcf.request",AT_FDCWD,claim,1))return;
 struct stat claimed;if(lstat(claim,&claimed)||claimed.st_ino!=st.st_ino||claimed.st_dev!=st.st_dev||claimed.st_size!=st.st_size)return;
 /* At most forwards. Crash/partial result is not acknowledged or retried. */
 ((void(*)(void*,int))(base+DCF_VOLUME_SET))(volume,(int)r.target+1);
 ((void(*)(void*,int))(base+DCF_GLOBAL_SET))(dcf,(int)r.target);
 ((void(*)(void*,int))(base+DCF_INDEX_SET))(index,(int)r.target);
 if(((int(*)(void*))(base+DCF_MAIN_INDEX))(main)!=(int)r.target||
    ((int(*)(void*))(base+DCF_VOLUME_INDEX))(volume)!=(int)r.target)return;
 fd=open(result,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd>=0){dprintf(fd,"DCF_ADVANCED %llu %u\n",(unsigned long long)r.token,r.target);fsync(fd);close(fd);}
 fprintf(stderr,"PS_DCF_ADVANCED from=%u to=%u NEXT=%u PERSIST_REQUESTED\n",r.expected,r.target,r.target+1);
}
