/* Native process adapter for the firmware's own published live-view command.
 * No shell expansion, no repeated writes on timeout; CLI output is bounded. */
#include "native_view_scope.h"
#include <sys/wait.h>
static int nvc_command(const char*method,char*out,size_t capacity){
 int pp[2];if(pipe2(pp,O_CLOEXEC|O_NONBLOCK))return 0;
 pid_t pid=fork();if(pid<0){close(pp[0]);close(pp[1]);return 0;}
 if(!pid){
  setpgid(0,0);close(pp[0]);fcntl(pp[1],F_SETFL,0);dup2(pp[1],1);dup2(pp[1],2);close(pp[1]);
  const char*tool="/system/bin/odindb-send";
  if(method)execl(tool,tool,"-s","camera","-m","set_live_view",method,(char*)NULL);
  else execl(tool,tool,"-s","camera","-p","live_view_state",(char*)NULL);
  _exit(127);
 }
 setpgid(pid,pid);close(pp[1]);size_t used=0;int status=0,done=0,overflow=0;double deadline=now()+7;
 while(now()<deadline){
  char chunk[256];ssize_t n;
  while((n=read(pp[0],chunk,sizeof chunk))>0){if((size_t)n>=capacity-used)overflow=1;else{memcpy(out+used,chunk,(size_t)n);used+=(size_t)n;}}
  if(waitpid(pid,&status,WNOHANG)==pid){done=1;break;}
  struct pollfd p={pp[0],POLLIN,0};poll(&p,1,20);
 }
 if(!done){kill(-pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}}
 char tail[256];ssize_t n;
 while((n=read(pp[0],tail,sizeof tail))>0){if((size_t)n>=capacity-used)overflow=1;else{memcpy(out+used,tail,(size_t)n);used+=(size_t)n;}}
 close(pp[0]);out[used]=0;return done&&!overflow&&WIFEXITED(status)&&!WEXITSTATUS(status);
}
static int nvc_get(void){
 char value[256];if(!nvc_command(NULL,value,sizeof value))return -1;
 if(!strcmp(value,"live_view_state = E_LiveViewState_Off(0)\n"))return 0;
 if(!strcmp(value,"live_view_state = E_LiveViewState_Active(1)\n"))return 1;
 return -1;
}
static int nvc_set(int enabled){
 char value[256];return nvc_command(enabled?"true":"false",value,sizeof value)&&!strcmp(value,"Ok\n");
}
