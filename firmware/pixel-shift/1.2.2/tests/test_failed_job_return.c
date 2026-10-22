#define PS_DATA "data"
#define PS_BIN "bin"
#define SHUTTER "hook"
#define PS_HOST_TEST_NONATOMIC_RENAME
#define PS_SERVICE_HOST_ALBUM_FAKE
#define main original_service_main
#include "../src/service/integration_service.c"
#undef main
#include <assert.h>
static void exercise(int code,unsigned id){
 char folder[128],text[32];jobid=id;busy=1;settled=0;recovery=0;armed=0;
 snprintf(folder,sizeof folder,"data/jobs/%u",id);assert(!mkdir(folder,0700));
 int f=open("data/pending",O_WRONLY|O_CREAT|O_EXCL,0600);assert(f>=0);
 int n=snprintf(text,sizeof text,"%u",id);assert(write(f,text,n)==n);close(f);
 child=fork();assert(child>=0);if(!child)_exit(code);
 for(int i=0;i<1000&&child>0;i++){reap_job();usleep(1000);}assert(child==-1&&(code==20?busy:!busy));
}
int main(void){
 char tmp[]="/tmp/x2d2-failed-return-XXXXXX";assert(mkdtemp(tmp));assert(!chdir(tmp));
 assert(!mkdir("data",0700));assert(!mkdir("data/jobs",0700));assert(!mkdir("hook",0700));
 session=123;
 /* Pre-capture failure must wait for menu Auto6 restoration, then archive. */
 int modefd=open(MODE_PREPARE_RECORD,O_CREAT|O_WRONLY,0600);assert(modefd>=0);close(modefd);
 exercise(20,10);assert(shot_waiting&&wait_cancel&&!recovery&&!settled);
 launch_waiting_job();assert(busy&&shot_waiting&&!access("data/pending",F_OK));
 mode_releasing=1;mode_prepare_started=clock_ms();mode_prepare_child=fork();assert(mode_prepare_child>=0);
 if(!mode_prepare_child)_exit(0); /* mock a confirmed settings restore */
 for(int i=0;i<1000&&mode_prepare_child>0;i++){update_mode_preparation();usleep(1000);}
 assert(mode_prepare_child<0&&!mode_record_exists());
 launch_waiting_job();assert(!busy&&!shot_waiting&&settled&&!recovery);
 assert(access("data/pending",F_OK)&&!access("data/jobs/10/cancelled.pending",F_OK));

 exercise(131,1);assert(settled&&!recovery&&!armed);assert(!strcmp(error_text,"FAILED_IDLE_INPUTS_RETAINED"));
 assert(!access("data/jobs/1/failed-idle.pending",F_OK)&&access("data/pending",F_OK));
 WireGate g;int f=open("hook/gate",O_RDONLY);assert(f>=0&&read(f,&g,sizeof g)==sizeof g);close(f);assert(g.mode==0);
 exercise(131,2);assert(settled&&!recovery);assert(!access("data/jobs/2/failed-idle.pending",F_OK));
 exercise(130,3);assert(settled&&!recovery);assert(!strcmp(error_text,"CANCELLED_INPUTS_RETAINED"));
 exercise(1,4);assert(!settled&&recovery);assert(!access("data/pending",F_OK));
 puts("PASS safe failure, normal gate, second task, cancellation, unresolved protection");return 0;
}
