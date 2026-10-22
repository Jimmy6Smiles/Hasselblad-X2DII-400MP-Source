#include "dcf_sync_contract.h"
static int dcf_index(unsigned *value){
 char text[128],tail;unsigned n;char *av[]={PS_ODIN,"-s","storage","-p","camera_file_index",NULL};
 if(!PS_ALBUM_COMMAND(av,text,sizeof text,6000,-1)||sscanf(text,"camera_file_index = %u %c",&n,&tail)!=1||n>=9999999)return 0;
 *value=n;return 1;
}
static int dcf_commit(const Settings *v,const char *stem,int journal){
 char folder[14],display[32],album[256],path[512];struct stat st;unsigned index;
 if(stopping||!album_names(v,stem,folder,display,album)||!dcf_index(&index))return 0;
 unsigned target=(unsigned)strtoul(stem+1,NULL,10);
 if(index>target)return checkpoint(journal,"DCF_ALREADY_AHEAD");
 snprintf(path,sizeof path,"%s/%s.%s",album,stem,v->format==2?"JPG":"3FR");if(!regular(path,&st))return 0;
 struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;
 PsDcfSync r={.magic=PS_DCF_MAGIC,.token=(uint64_t)ts.tv_sec*1000000000+ts.tv_nsec,
 .deadline_ms=(uint64_t)clock_ms()+60000,.device=st.st_dev,.inode=st.st_ino,.bytes=st.st_size,.expected=index,.target=target,.reserved=v->format==2?1:0};
 memcpy(r.folder,folder,14);if(!ps_dcf_valid(&r,folder,(uint64_t)clock_ms()))return 0;
 char tmp[256],result[256],expected[96],reply[96];
 snprintf(tmp,sizeof tmp,PS_ALBUM_REFRESH_ROOT"/dcf-%016llx.tmp",(unsigned long long)r.token);
 snprintf(result,sizeof result,PS_ALBUM_REFRESH_ROOT"/dcf-%016llx.result",(unsigned long long)r.token);
 if(dprintf(journal,"DCF_SYNC_INTENT %llu %u %u\n",(unsigned long long)r.token,index,target)<0||fsync(journal))return 0;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 int ok=write(fd,&r,sizeof r)==sizeof r&&!fsync(fd);close(fd);
 if(!ok||!exclusive_rename(tmp,PS_ALBUM_REFRESH_ROOT"/dcf.request"))return 0;
 char *av[]={PS_ODIN,"-s","storage","-m","browse",folder,"E_MetadataOptions_None",NULL};
 if(!PS_ALBUM_COMMAND(av,NULL,0,6000,-1))return 0;
 fd=open(result,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&st.st_size>0&&st.st_size<(off_t)sizeof reply)n=read(fd,reply,sizeof reply-1);
 close(fd);if(n<=0||n>=(ssize_t)sizeof reply)return 0;reply[n]=0;
 snprintf(expected,sizeof expected,"DCF_ADVANCED %llu %u\n",(unsigned long long)r.token,target);
 if(strcmp(reply,expected)||!dcf_index(&index)||index!=target)return 0;
 return checkpoint(journal,"DCF_OUTPUT_NUMBER_COMMITTED");
}
