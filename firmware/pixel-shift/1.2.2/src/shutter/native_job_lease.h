/* Shared worker/hook lease. Root-owned RAM file, bound to a live worker and
 * this monotonic boot clock. A valid token alone never authorizes a request. */
#ifndef NATIVE_JOB_LEASE_H
#define NATIVE_JOB_LEASE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#define NJL_ROOT "/dev/x2d2-pregdc-trial"
#define NJL_PATH NJL_ROOT "/job.lease"
typedef struct {uint64_t magic,nonce,deadline,start;uint32_t pid,reserved;} NativeJobLease;
static uint64_t njl_clock(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static uint64_t njl_process_start(unsigned pid){
 char path[64],line[2048];snprintf(path,sizeof path,"/proc/%u/stat",pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n<=0)return 0;line[n]=0;
 char*p=strrchr(line,')');if(!p||p[1]!=' ')return 0;p+=2;
 /* p starts at field 3; starttime is field 22. */
 for(unsigned field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;p++;}
 uint64_t v=0;unsigned digits=0;while(*p>='0'&&*p<='9'){if(v>UINT64_MAX/10)return 0;v=v*10+(unsigned)(*p++-'0');digits++;}
 return digits&&*p==' '?v:0;
}
static int njl_valid(const NativeJobLease*l,uint64_t now){
 return l&&l->magic==UINT64_C(0x314c4a4e443258)&&l->nonce&&!l->reserved&&l->pid>1&&l->start&&
 now&&l->deadline>now&&l->deadline-now<=300000&&njl_process_start(l->pid)==l->start;
}
static int njl_read(NativeJobLease*l){
 int fd=open(NJL_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;struct stat s;
 int ok=!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_uid==0&&s.st_nlink==1&&!(s.st_mode&0077)&&s.st_size==sizeof *l&&read(fd,l,sizeof *l)==sizeof *l;
 close(fd);return ok&&njl_valid(l,njl_clock());
}
static int njl_create(NativeJobLease*l,uint64_t nonce){
 *l=(NativeJobLease){.magic=UINT64_C(0x314c4a4e443258),.nonce=nonce,.deadline=njl_clock()+240000,.pid=(unsigned)getpid(),.start=njl_process_start((unsigned)getpid())};
 if(!njl_valid(l,njl_clock()))return 0;
 int fd=open(NJL_PATH,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
 int ok=write(fd,l,sizeof *l)==sizeof *l&&!fsync(fd);close(fd);if(!ok)unlink(NJL_PATH);return ok;
}
static int njl_remove(const NativeJobLease*owned){
 NativeJobLease current;int fd=open(NJL_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 int ok=read(fd,&current,sizeof current)==sizeof current&&!memcmp(&current,owned,sizeof current);close(fd);
 return ok&&!unlink(NJL_PATH);
}
#endif
