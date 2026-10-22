#if defined(__ANDROID__)
#error host fixture must never enter Android builds
#endif
#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static int pipe2(int p[2],int flags){
 if(pipe(p))return -1;
 for(int i=0;i<2;i++)if(fcntl(p[i],F_SETFD,(flags&O_CLOEXEC)?FD_CLOEXEC:0)<0||fcntl(p[i],F_SETFL,flags&O_NONBLOCK)<0){int e=errno;close(p[0]);close(p[1]);errno=e;return -1;}
 return 0;
}
static int accept4(int fd,struct sockaddr *addr,socklen_t *len,int flags){
 (void)flags;int c=accept(fd,addr,len);if(c>=0&&fcntl(c,F_SETFD,FD_CLOEXEC)<0){int e=errno;close(c);errno=e;return -1;}return c;
}
#endif
