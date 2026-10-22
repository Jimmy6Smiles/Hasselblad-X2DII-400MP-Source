/* 请求必须完整写入后才对消费者可见，且不得覆盖未领取请求。 */
#ifndef PS_REQUEST_PUBLISH_H
#define PS_REQUEST_PUBLISH_H
#include <sys/syscall.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static int ps_publish_request(const char *stage,const char *target,const void *data,size_t size){
 int fd=open(stage,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==getuid();
 size_t used=0;
 while(ok&&used<size){ssize_t n=write(fd,(const char*)data+used,size-used);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}used+=(size_t)n;}
 if(ok)ok=!fsync(fd);
 if(close(fd))ok=0;
 /* 失败保留独占创建的临时请求用于诊断，绝不降级为覆盖式 rename。 */
 return ok&&!syscall(SYS_renameat2,AT_FDCWD,stage,AT_FDCWD,target,1);
}
#endif
