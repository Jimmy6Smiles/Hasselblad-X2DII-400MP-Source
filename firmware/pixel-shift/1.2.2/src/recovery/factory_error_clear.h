/* Strict, bounded, read-only query; query failure is not 'no error'. */
static int factory_error_clear(void){
 int pp[2];if(pipe2(pp,O_CLOEXEC|O_NONBLOCK))return 0;
 pid_t p=fork();if(p<0){close(pp[0]);close(pp[1]);return 0;}
 if(!p){setpgid(0,0);close(pp[0]);fcntl(pp[1],F_SETFL,0);dup2(pp[1],1);dup2(pp[1],2);close(pp[1]);execl(ODIN_PATH,ODIN_PATH,"-s","error","-p","active_error",(char*)NULL);_exit(127);}
 setpgid(p,p);close(pp[1]);int status=0,done=0,eof=0,ok=1;char b[128];size_t used=0;int64_t end=now()+2000;
 while(!done||!eof){
  char piece[64];ssize_t n=read(pp[0],piece,sizeof piece);
  if(n>0){if(used+(size_t)n>=sizeof b){ok=0;break;}memcpy(b+used,piece,(size_t)n);used+=(size_t)n;}
  else if(!n)eof=1;else if(errno!=EINTR&&errno!=EAGAIN){ok=0;break;}
  if(!done){pid_t got=waitpid(p,&status,WNOHANG);if(got==p)done=1;else if(got<0&&errno!=EINTR){ok=0;break;}}
  if(done&&eof)break;
  if(now()>=end){ok=0;break;}struct pollfd f={pp[0],POLLIN,0};poll(&f,1,20);
 }
 if(!ok){kill(-p,SIGKILL);kill(p,SIGKILL);}
 if(!done)while(waitpid(p,&status,0)<0&&errno==EINTR){}
 close(pp[0]);b[used]=0;
 return ok&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&!strcmp(b,"active_error = false\n");
}
