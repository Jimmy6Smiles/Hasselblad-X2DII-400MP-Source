#define PS_DATA "data"
#define PS_BIN "bin"
#define SHUTTER "hook"
#define PS_HOST_TEST_NONATOMIC_RENAME
#define PS_SERVICE_HOST_ALBUM_FAKE
#define main original_service_main
#include "../src/service/integration_service.c"
#undef main
#include <assert.h>
static void result(int code){mode_releasing=1;mode_prepare_started=clock_ms();mode_prepare_child=fork();assert(mode_prepare_child>=0);if(!mode_prepare_child)_exit(code);for(int i=0;i<1000&&mode_prepare_child>0;i++){update_mode_preparation();usleep(1000);}assert(mode_prepare_child<0);}
int main(void){char temp[]="/tmp/mode-retry-XXXXXX";assert(mkdtemp(temp)&&!chdir(temp));assert(!mkdir("data",0700));
 int fd=open(MODE_PREPARE_RECORD,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
 result(4);assert(!recovery&&mode_release_failures==1&&mode_record_exists());
 result(0);assert(!recovery&&!mode_record_exists()&&!mode_release_failures&&!error_text[0]);
 fd=open(MODE_PREPARE_RECORD,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
 result(4);result(4);assert(!recovery);result(4);assert(recovery&&mode_record_exists());
 assert(mode_restore_retry_allowed());
 busy=1;assert(!mode_restore_retry_allowed());busy=0;
 busy=shot_waiting=wait_cancel=1;assert(mode_restore_retry_allowed());
 child=123;assert(!mode_restore_retry_allowed());child=-1;
 wait_cancel=0;assert(!mode_restore_retry_allowed());wait_cancel=1;
 result(0);assert(!recovery&&!mode_record_exists());
 assert(!mkdir("hook",0700));assert(exact_write("data/pending","123",3));
 assert(!mkdir("data/jobs",0700));jobid=123;
 note_cancel("MODE_PREPARE_FAILED",256);note_cancel("API_CANCEL_REQUEST",0);
 assert(!strcmp(cancel_cause,"MODE_PREPARE_FAILED detail=256"));
 launch_waiting_job();assert(!busy&&!shot_waiting&&settled&&!recovery);
 fd=open("data/jobs/123/cancel-reason.log",O_RDONLY);assert(fd>=0);
 char reason[512]={0};assert(read(fd,reason,sizeof reason-1)>0);close(fd);
 assert(strstr(reason,"MODE_PREPARE_FAILED detail=256"));
 assert(access("data/pending",F_OK)&&!access("data/jobs/123/cancelled.pending",F_OK));
 fd=open(MODE_PREPARE_RECORD,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
 result(4);result(4);result(4);assert(recovery);
 boot_recovery_block=1;assert(!mode_restore_retry_allowed());boot_recovery_block=0;
 strcpy(error_text,"OTHER_RECOVERY");assert(!mode_restore_retry_allowed());
 strcpy(error_text,"MODE_PREPARE_RESTORE_PENDING");
 update_mode_preparation();assert(mode_prepare_child<0); /* cooldown: no spin */
 result(0);assert(!recovery&&!mode_record_exists()&&!error_text[0]);
 fd=open(MODE_PREPARE_RECORD,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
 recovery=1;strcpy(error_text,"OTHER_RECOVERY");result(0);assert(recovery&&!strcmp(error_text,"OTHER_RECOVERY"));
 puts("MODE_RETRY_TEST_PASS");}
