#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include "../src/coordinator/native_service_quiesce.h"
static const char *factory="",*trial="running";
int __system_property_get(const char*k,char*v){
 const char*s=!strcmp(k,"init.svc.camera-service")?factory:trial;
 strcpy(v,s);return (int)strlen(s);
}
int __system_property_set(const char*k,const char*v){(void)k;(void)v;abort();}
int main(void){
 factory="";assert(nsq_factory_inactive());
 factory="stopped";assert(nsq_factory_inactive());
 factory="running";assert(!nsq_factory_inactive());
 factory="restarting";assert(!nsq_factory_inactive());
 factory="stopping";assert(!nsq_factory_inactive());
 factory="invalid";assert(!nsq_factory_inactive());
 factory="";NativeServiceIdentity id;
 /* Missing factory property alone must never authorize native rendering. */
 trial="stopped";assert(!nsq_identify(&id));
 trial="running";assert(!nsq_identify(&id)); /* no camera process on host */
 puts("PASS 8 direct-service state cases");return 0;
}
