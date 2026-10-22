/* Rebuild three maps; replace input with our retained reference's export and
 * output with our new buffer's export. Geometry and pixel data unchanged.
 * DCAM_CONTAINER is copied byte-for-byte in RAM, never serialized to a file. */
#include "native_dcam_fixture.h"
extern const char *dbus_message_get_path(void*);
extern const char *dbus_message_get_interface(void*);
extern const char *dbus_message_get_member(void*);
extern void dbus_message_iter_init_append(void*,Iter*);
extern int dbus_message_iter_open_container(Iter*,int,const char*,Iter*);
extern int dbus_message_iter_close_container(Iter*,Iter*);
extern int dbus_message_iter_append_basic(Iter*,int,const void*);
extern int dbus_message_iter_append_fixed_array(Iter*,int,const void*,int);
extern void dbus_message_iter_abandon_container_if_open(Iter*,Iter*);
extern void dbus_message_iter_get_fixed_array(Iter*,void*,int*);
static void* clone_native_metadata(void*event,uint64_t input,uint64_t output,const void*metadata,int metadata_bytes){
 if((metadata==NULL)!=(metadata_bytes==0)||metadata_bytes<0||metadata_bytes>32768)return NULL;
 if(!input||!output||strcmp(dbus_message_get_path(event),"/camera")||
    strcmp(dbus_message_get_interface(event),"com.hasselblad.camera")||
    strcmp(dbus_message_get_member(event),"reprocess_exposure_item"))return NULL;
 void*m=dbus_message_new_method_call("com.hasselblad.camera","/camera","com.hasselblad.camera","reprocess_exposure_item");
 if(!m)return NULL;
 Iter src={0},dst={0},to={0},entry={0},variant={0},array={0};unsigned index=0,changed[2]={0,0};
 if(!dbus_message_iter_init(event,&src))goto fail;
 dbus_message_iter_init_append(m,&dst);
 do{
  if(index>=3||dbus_message_iter_get_arg_type(&src)!='a')goto fail;
  Iter from={0};memset(&to,0,sizeof to);unsigned count=0;dbus_message_iter_recurse(&src,&from);
  if(!dbus_message_iter_open_container(&dst,'a',"{sv}",&to))goto fail;
  while(dbus_message_iter_get_arg_type(&from)){
   Iter pair={0},v={0};memset(&entry,0,sizeof entry);memset(&variant,0,sizeof variant);memset(&array,0,sizeof array);const char*k=NULL;
   if(++count>64||dbus_message_iter_get_arg_type(&from)!='e')goto fail;
   dbus_message_iter_recurse(&from,&pair);
   if(dbus_message_iter_get_arg_type(&pair)!='s')goto fail;
   dbus_message_iter_get_basic(&pair,&k);
   if(!k||strnlen(k,96)>=96||!dbus_message_iter_next(&pair)||dbus_message_iter_get_arg_type(&pair)!='v')goto fail;
   dbus_message_iter_recurse(&pair,&v);int type=dbus_message_iter_get_arg_type(&v);
   if(index==1){if(strcmp(k,"DCAM_CONTAINER")||type!='a'||count!=1)goto fail;}
   else if(type!='i'&&type!='u'&&type!='t'&&type!='s')goto fail;
   char signature[3]={(char)type,0,0};if(type=='a')signature[1]='y';
   if(!dbus_message_iter_open_container(&to,'e',NULL,&entry)||!dbus_message_iter_append_basic(&entry,'s',&k)||
      !dbus_message_iter_open_container(&entry,'v',signature,&variant))goto fail;
   if(index!=1&&!strcmp(k,"SHARE_HANDLE")){
    uint64_t replacement=index==0?input:output;
    if(type!='t'||changed[index/2]++||!dbus_message_iter_append_basic(&variant,'t',&replacement))goto fail;
   }else if(type=='a'){
    Iter bytes={0};const void*data=NULL;int n=0;dbus_message_iter_recurse(&v,&bytes);
    if(dbus_message_iter_get_arg_type(&bytes)!='y')goto fail;
    dbus_message_iter_get_fixed_array(&bytes,&data,&n);
    if(metadata){if(!nd_validate(metadata,(size_t)metadata_bytes))goto fail;data=metadata;n=metadata_bytes;}
    if(!data||n<=0||n>16777216||!dbus_message_iter_open_container(&variant,'a',"y",&array)||
       !dbus_message_iter_append_fixed_array(&array,'y',&data,n)||!dbus_message_iter_close_container(&variant,&array))goto fail;
   }else if(type=='s'){
    const char*s=NULL;dbus_message_iter_get_basic(&v,&s);
    if(!s||strnlen(s,4096)>=4096||!dbus_message_iter_append_basic(&variant,type,&s))goto fail;
   }else{
    uint64_t n=0;dbus_message_iter_get_basic(&v,&n);if(!dbus_message_iter_append_basic(&variant,type,&n))goto fail;
   }
   if(dbus_message_iter_next(&v)||dbus_message_iter_next(&pair)||!dbus_message_iter_close_container(&entry,&variant)||
      !dbus_message_iter_close_container(&to,&entry))goto fail;
   if(!dbus_message_iter_next(&from))break;
  }
  if(!count||!dbus_message_iter_close_container(&dst,&to))goto fail;
  index++;
 }while(dbus_message_iter_next(&src));
 if(index!=3||changed[0]!=1||changed[1]!=1)goto fail;
 return m;
fail:
 dbus_message_iter_abandon_container_if_open(&variant,&array);
 dbus_message_iter_abandon_container_if_open(&entry,&variant);
 dbus_message_iter_abandon_container_if_open(&to,&entry);
 dbus_message_iter_abandon_container_if_open(&dst,&to);
 dbus_message_unref(m);return NULL;
}
static void* clone_native(void*event,uint64_t input,uint64_t output){return clone_native_metadata(event,input,output,NULL,0);}
