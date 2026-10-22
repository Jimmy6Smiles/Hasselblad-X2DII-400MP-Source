#define PS_DATA "data"
#define PS_BIN "bin"
#define SHUTTER "hook"
#define PS_HOST_TEST_NONATOMIC_RENAME
#define PS_SERVICE_HOST_ALBUM_FAKE
#define main original_service_main
#include "../src/service/integration_service.c"
#undef main
#include <assert.h>
int main(void){
 char tmp[]="/tmp/ps-boot-review-XXXXXX";assert(mkdtemp(tmp)&&!chdir(tmp));
 assert(!mkdir("data",0700)&&!mkdir("bin",0700)&&!mkdir("hook",0700));
 assert(exact_write(MODE_PREPARE_RECORD,"retained",8));
 assert(exact_write("data/pending","123",3));
 const char *script="#!/bin/sh\nexit 4\n";
 assert(exact_write("bin/reboot-reconcile",script,strlen(script)));
 assert(!chmod("bin/reboot-reconcile",0700));
 boot_recovery_block=recovery=1;session=123;
 assert(mode_preparation_blocks_normal()&&!live_hook(0));assert(gate(2,0));
 update_boot_recovery();assert(boot_review_child>0);
 for(int i=0;i<1000&&boot_review_child>0;i++){update_boot_recovery();usleep(1000);}
 assert(boot_review_child<0&&recovery&&boot_recovery_block);
 assert(mode_record_exists()&&!access("data/pending",F_OK));
 WireGate g;int fd=open("hook/gate",O_RDONLY);assert(fd>=0&&read(fd,&g,sizeof g)==sizeof g);close(fd);assert(g.mode==2);
 puts("PASS boot review starts despite BUSY gate; unresolved state stays blocked");
}
