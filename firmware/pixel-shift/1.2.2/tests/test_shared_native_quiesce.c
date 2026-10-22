#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#define NATIVE_JOB_LEASE_H
static uint64_t njl_clock(void){return 1000;}
static uint64_t njl_process_start(unsigned pid){(void)pid;return 99;}
static int runtime_present, runtime_error, stopped, calls;
static int probe(const char *path,struct stat *st){
 (void)st;
 if(!strcmp(path,"/system/camera-extension-host-v1")){
  if(runtime_present)return 0;
  errno=runtime_error;return -1;
 }
 assert(!strcmp(path,"/proc/123"));
 if(stopped){errno=ENOENT;return -1;}
 return 0;
}
int __system_property_get(const char *key,char *value){
 assert(!strcmp(key,"init.svc.x2d2-capture-trial"));
 strcpy(value,stopped?"stopped":"running");return (int)strlen(value);
}
int __system_property_set(const char *key,const char *value){
 assert(!strcmp(key,"ctl.stop")&&!strcmp(value,"x2d2-capture-trial"));
 ++calls;stopped=1;return 0;
}
#define lstat probe
#ifndef TEST_QUIESCE_HEADER
#define TEST_QUIESCE_HEADER "../src/coordinator/native_service_quiesce.h"
#endif
#include TEST_QUIESCE_HEADER
int main(void){
 NativeServiceIdentity shared={123,99,1},legacy={123,99,0};
 runtime_error=ENOENT;runtime_present=1;
 assert(!nsq_stop(&shared)&&!calls);
 assert(!nsq_stop(&legacy)&&!calls); /* takeover after identity capture */
 runtime_present=0;runtime_error=EACCES;
 assert(!nsq_stop(&legacy)&&!calls);
 runtime_error=ENOENT;
 assert(!nsq_stop(&shared)&&!calls); /* pinned owner cannot downgrade */
 assert(!nsq_stop(NULL)&&!calls);
 assert(nsq_stop(&legacy)&&calls==1&&stopped);
 assert(nsq_gone(&shared)); /* release only after old process is gone */
 puts("SHARED_NATIVE_NO_SERVICE_STOP_RETAINS_OWNER_LEGACY_COMPAT_PASS");
 return 0;
}
