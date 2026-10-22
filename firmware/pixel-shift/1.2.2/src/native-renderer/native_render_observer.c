/* X2D II passive native-render descriptor observer. No capture/render requests,
 * no shared-buffer imports, metadata bytes, settings, or security-policy writes.
 * Protocol reference: user-supplied X2D factory_request_observer.c in research ZIP.
 * Uses public libdbus ABI; firmware addresses are deliberately not reused. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
typedef struct { uint64_t opaque[16]; } Iter;
extern void *dbus_bus_get_private(int,void*);
extern void dbus_connection_set_exit_on_disconnect(void*,int);
extern void *dbus_message_new_method_call(const char*,const char*,const char*,const char*);
extern int dbus_message_append_args(void*,int,...);
extern void *dbus_connection_send_with_reply_and_block(void*,void*,int,void*);
extern int dbus_message_get_type(void*);
extern int dbus_message_is_method_call(void*,const char*,const char*);
extern int dbus_connection_read_write(void*,int);
extern void *dbus_connection_pop_message(void*);
extern int dbus_message_iter_init(void*,Iter*);
extern int dbus_message_iter_get_arg_type(Iter*);
extern int dbus_message_iter_next(Iter*);
extern void dbus_message_iter_recurse(Iter*,Iter*);
extern void dbus_message_iter_get_basic(Iter*,void*);
extern void dbus_message_unref(void*);
extern void dbus_connection_close(void*);
extern void dbus_connection_unref(void*);
static volatile sig_atomic_t stop;
static void stopped(int sig){(void)sig;stop=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int geometry(const char*k){
 const char*keys[]={"WIDTH","HEIGHT","STRIDE","FORMAT","SRC_FORMAT","CROP_X","CROP_Y","CROP_W","CROP_H","DATA_SIZE","REQUESTED_SIZE","PLANE_0_WIDTH","PLANE_0_HEIGHT","PLANE_0_OFFSET","PLANE_0_CONTENT_SIZE","PLANE_1_WIDTH","PLANE_1_HEIGHT","PLANE_1_OFFSET","PLANE_1_CONTENT_SIZE"};
 for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)if(!strcmp(k,keys[i]))return 1;
 return 0;
}
#ifdef NATIVE_IMPORT_SIZES
#include "native_render_import.h"
#endif
#ifdef NATIVE_EXPORT_TEMPLATE
#include "native_template_export.h"
#endif
static int schema(void*m){
 Iter args={0};unsigned a=0;
 if(!dbus_message_iter_init(m,&args))return 20;
 do{
  if(a>=3||dbus_message_iter_get_arg_type(&args)!='a')return 21;
  Iter map={0};dbus_message_iter_recurse(&args,&map);unsigned count=0;
  printf("MAP %u\n",a);
  while(dbus_message_iter_get_arg_type(&map)){
   if(++count>64||dbus_message_iter_get_arg_type(&map)!='e')return 22;
   Iter pair={0},value={0};const char*key=NULL;dbus_message_iter_recurse(&map,&pair);
   if(dbus_message_iter_get_arg_type(&pair)!='s')return 23;
   dbus_message_iter_get_basic(&pair,&key);
   if(!key||strnlen(key,96)>=96)return 24;
   for(const char*p=key;*p;p++)if(*p<32||*p>126)return 24;
   if(!dbus_message_iter_next(&pair)||dbus_message_iter_get_arg_type(&pair)!='v')return 25;
   dbus_message_iter_recurse(&pair,&value);int typ=dbus_message_iter_get_arg_type(&value);
   printf("KEY %s TYPE %d\n",key,typ);
   if(a!=1&&geometry(key)){
    if(typ=='u'){uint32_t n=0;dbus_message_iter_get_basic(&value,&n);printf("GEOMETRY %s %u\n",key,n);}
    else if(typ=='i'){int32_t n=0;dbus_message_iter_get_basic(&value,&n);printf("GEOMETRY %s %d\n",key,n);}
   }
   if(!dbus_message_iter_next(&map))break;
  }
  a++;
 }while(dbus_message_iter_next(&args));
 return a==3?0:26;
}
int main(int argc,char**argv){
 if(argc!=2||strcmp(argv[1],"--observe-once"))return 1;
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stopped);signal(SIGINT,stopped);
 void*c=dbus_bus_get_private(1,NULL),*m=NULL,*r=NULL;int rc=2;
 if(!c){puts("BUS_UNAVAILABLE");return rc;}
 dbus_connection_set_exit_on_disconnect(c,0);
 const char*rule="type='method_call',sender='com.hasselblad.storage',interface='com.hasselblad.camera',member='reprocess_exposure_item',eavesdrop='true'";
 m=dbus_message_new_method_call("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","AddMatch");
 if(!m||!dbus_message_append_args(m,'s',&rule,0))goto end;
 r=dbus_connection_send_with_reply_and_block(c,m,3000,NULL);rc=3;
 if(!r||dbus_message_get_type(r)!=2){puts("MATCH_DENIED_NO_POLICY_CHANGE");goto end;}
#ifdef NATIVE_REPEAT
 if(!reply_match(c)){rc=4;goto end;}
#endif
 printf("READY_PASSIVE %.3f\n",now());double deadline=now()+120;rc=6;
 while(!stop&&now()<deadline){
  if(!dbus_connection_read_write(c,100))break;
  void*event;
  while((event=dbus_connection_pop_message(c))){
   if(dbus_message_is_method_call(event,"com.hasselblad.camera","reprocess_exposure_item")){
    rc=schema(event);
#ifdef NATIVE_EXPORT_TEMPLATE
    if(!rc)rc=export_template(event);
#endif
#ifdef NATIVE_IMPORT_SIZES
    if(!rc)rc=import_sizes(c,event);
#endif
    dbus_message_unref(event);goto end;
   }
   dbus_message_unref(event);
  }
 }
end:
 if(r)dbus_message_unref(r);if(m)dbus_message_unref(m);
 dbus_connection_close(c);dbus_connection_unref(c);
 printf("OBSERVER_EXIT %d NO_CAPTURE\n",rc);return rc;
}
