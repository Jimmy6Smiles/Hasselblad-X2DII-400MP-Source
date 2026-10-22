/* Menu selection owns preparation, never exposure or exposure lock. */
static pid_t mode_prepare_child=-1;
static int mode_prepared,mode_releasing;
static int64_t mode_prepare_started;
static unsigned mode_release_failures;
static int64_t mode_release_retry_at;
#define MODE_PREPARE_RECORD PS_DATA "/mode-prepare.journal"
static int mode_record_exists(void){struct stat st;return lstat(MODE_PREPARE_RECORD,&st)==0;}
static int mode_preparation_ready(void){return mode_prepared&&mode_prepare_child<0&&mode_record_exists();}
static int mode_preparation_blocks_normal(void){return mode_prepare_child>0||mode_prepared||mode_record_exists();}
static int mode_restore_retry_allowed(void){
 /* A rejected pre-capture child still owns a waiting cancellation. It must
  * retry menu restoration after the third transient failure too. */
 return recovery&&!boot_recovery_block&&!armed&&child<0&&
  ((!busy&&!shot_waiting)||(busy&&shot_waiting&&wait_cancel))&&
  !strcmp(error_text,"MODE_PREPARE_RESTORE_PENDING")&&mode_record_exists();
}
static void update_mode_preparation(void){
 if(mode_prepare_child>0){
  int result=0;pid_t got=waitpid(mode_prepare_child,&result,WNOHANG);
  if(got==mode_prepare_child){
   mode_prepare_child=-1;
   if(WIFEXITED(result)&&WEXITSTATUS(result)==0){
    if(mode_releasing){
     if(unlink(MODE_PREPARE_RECORD)&&errno!=ENOENT){recovery=1;strcpy(error_text,"MODE_PREPARE_CLEANUP_FAILED");}
     mode_prepared=0;
     mode_release_failures=0;
     if(!strcmp(error_text,"MODE_PREPARE_RESTORE_RETRY")||
        !strcmp(error_text,"MODE_PREPARE_RESTORE_PENDING")){
      recovery=0;error_text[0]=0;readiness_checked=0;
     }
    }else mode_prepared=1;
   }else{
    mode_prepared=0;armed=0;
    if(mode_releasing){
     if(mode_release_failures<3)mode_release_failures++;
     mode_release_retry_at=clock_ms()+(mode_release_failures>=3?5000:500);
     if(mode_release_failures>=3){recovery=1;strcpy(error_text,"MODE_PREPARE_RESTORE_PENDING");}
     else strcpy(error_text,"MODE_PREPARE_RESTORE_RETRY");
    }
    else{strcpy(error_text,"MODE_PREPARE_FAILED");if(shot_waiting)note_cancel("MODE_PREPARE_FAILED",result);}
   }
  }else if(got<0&&errno!=EINTR){mode_prepare_child=-1;armed=0;recovery=1;strcpy(error_text,"MODE_PREPARE_CHILD_UNKNOWN");}
  else if(clock_ms()-mode_prepare_started>(mode_releasing?30000:15000))kill(mode_prepare_child,SIGTERM);
  return;
 }
 if((busy&&!shot_waiting)||(recovery&&!mode_restore_retry_allowed()))return;
 int release=((!armed&&!shot_waiting)||stopping||wait_cancel)&&mode_record_exists();
 if(release&&clock_ms()<mode_release_retry_at)return;
 if(!release&&((!armed&&!shot_waiting)||stopping||wait_cancel||mode_prepared||!live_hook(shot_waiting?2:1)||(!shot_waiting&&!fast_blocked())))return;
 mode_releasing=release;mode_prepare_started=clock_ms();mode_prepare_child=fork();
 if(!mode_prepare_child){
  setpgid(0,0);close(listenfd);
  int fd=open(PS_BIN"/mode-prepare.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)_exit(120);dup2(fd,1);dup2(fd,2);close(fd);
  dprintf(1,"MODE_OPERATION release=%d attempt=%u\n",release,mode_release_failures+1);
  execl(PS_BIN"/native-auto6",PS_BIN"/native-auto6",release?"--release-mode":"--prepare-mode",MODE_PREPARE_RECORD,(char*)0);_exit(127);
 }
 if(mode_prepare_child>0)setpgid(mode_prepare_child,mode_prepare_child);
 else{recovery=1;armed=0;strcpy(error_text,"MODE_PREPARE_FORK_FAILED");}
}
