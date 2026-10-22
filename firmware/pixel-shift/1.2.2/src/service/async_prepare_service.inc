/* Selection is not readiness. Only the event loop owns these child states. */
static pid_t menu_check_child=-1;
static int menu_check_ok,shot_waiting,wait_cancel;
static char cancel_cause[128];
static void note_cancel(const char *reason,int detail){
 if(!cancel_cause[0])snprintf(cancel_cause,sizeof cancel_cause,"%s detail=%d",reason,detail);
 wait_cancel=1;
}
static int64_t menu_check_started,menu_check_finished;
static void update_menu_check(void){
 if(menu_check_child>0){
  int result=0;pid_t got=waitpid(menu_check_child,&result,WNOHANG);
  if(got==menu_check_child){
   menu_check_child=-1;menu_check_finished=clock_ms();
   menu_check_ok=WIFEXITED(result)&&WEXITSTATUS(result)==0;
   if(!menu_check_ok){
    if(shot_waiting)note_cancel("MENU_STORAGE_PREPARATION_FAILED",result);
    else armed=0;
    strcpy(error_text,"MENU_STORAGE_PREPARATION_FAILED");
   }
  }else if(got<0&&errno!=EINTR){menu_check_child=-1;menu_check_ok=0;armed=0;if(shot_waiting)note_cancel("MENU_WAITPID_FAILED",errno);}
  else if(clock_ms()-menu_check_started>12000){kill(-menu_check_child,SIGKILL);kill(menu_check_child,SIGKILL);}
  return;
 }
 if(stopping||recovery||wait_cancel||(!armed&&!shot_waiting)||
    (busy&&!shot_waiting)||(menu_check_ok&&(shot_waiting||clock_ms()-menu_check_finished<3000)))return;
 menu_check_started=clock_ms();menu_check_child=fork();
 if(!menu_check_child){
  setpgid(0,0);close(listenfd);
  int fd=open(PS_BIN"/menu-storage-prepare.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)_exit(120);dup2(fd,1);dup2(fd,2);close(fd);
  execl(PS_BIN"/integrated-job",PS_BIN"/integrated-job","--preflight",(char*)0);_exit(127);
 }
 if(menu_check_child>0)setpgid(menu_check_child,menu_check_child);
 else{menu_check_ok=0;armed=0;if(shot_waiting)note_cancel("MENU_PREPARATION_FORK_FAILED",errno);strcpy(error_text,"MENU_PREPARATION_FORK_FAILED");}
}
static void stop_menu_check(void){
 if(menu_check_child>0){kill(-menu_check_child,SIGTERM);kill(menu_check_child,SIGTERM);}
 menu_check_ok=0;
}
