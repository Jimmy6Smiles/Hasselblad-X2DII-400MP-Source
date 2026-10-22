/* Local fixture parser. No bus connection, does not send the invalid handles. */
#ifndef NATIVE_TEMPLATE_LOAD_H
#define NATIVE_TEMPLATE_LOAD_H
#include "native_dcam_fixture.h"
extern void*dbus_message_demarshal(const char*,int,void*);
extern void dbus_message_iter_get_fixed_array(Iter*,void*,int*);
static void*nt_load(const char*path,unsigned char*metadata,size_t capacity,size_t*length){
 unsigned char wire[32769];FILE*f=fopen(path,"rb");if(!f)return NULL;
 size_t bytes=fread(wire,1,sizeof wire,f);int error=ferror(f);fclose(f);
 if(error||bytes<16||bytes>32768||wire[0]!='l')return NULL;
 if(!nd_u32(wire+8))nd_put(wire+8,1);
 void*m=dbus_message_demarshal((const char*)wire,(int)bytes,NULL);if(!m)return NULL;
 Iter args={0};unsigned index=0,handles=0,meta=0;
 if(!dbus_message_iter_init(m,&args))goto fail;
 do{
  if(index>2||dbus_message_iter_get_arg_type(&args)!='a')goto fail;
  Iter map={0};dbus_message_iter_recurse(&args,&map);
  while(dbus_message_iter_get_arg_type(&map)){
   Iter pair={0},v={0};const char*k=NULL;
   if(dbus_message_iter_get_arg_type(&map)!='e')goto fail;dbus_message_iter_recurse(&map,&pair);
   if(dbus_message_iter_get_arg_type(&pair)!='s')goto fail;dbus_message_iter_get_basic(&pair,&k);
   if(!k||!dbus_message_iter_next(&pair)||dbus_message_iter_get_arg_type(&pair)!='v')goto fail;
   dbus_message_iter_recurse(&pair,&v);
   if(index!=1&&!strcmp(k,"SHARE_HANDLE")){
    uint64_t h=0;if(dbus_message_iter_get_arg_type(&v)!='t'||(handles&(1u<<index)))goto fail;
    dbus_message_iter_get_basic(&v,&h);if(h!=UINT64_MAX)goto fail;handles|=1u<<index;
   }else if(index!=1&&!strcmp(k,"DUSS_OFFSET")){
    uint64_t off=0;if(dbus_message_iter_get_arg_type(&v)!='t')goto fail;
    dbus_message_iter_get_basic(&v,&off);if(off)goto fail;
   }else if(index==1){
    if(strcmp(k,"DCAM_CONTAINER")||meta++||dbus_message_iter_get_arg_type(&v)!='a')goto fail;
    Iter array={0};const unsigned char*p=NULL;int n=0;dbus_message_iter_recurse(&v,&array);
    if(dbus_message_iter_get_arg_type(&array)!='y')goto fail;dbus_message_iter_get_fixed_array(&array,&p,&n);
    if(n<=0||(size_t)n>capacity||!nd_validate(p,(size_t)n))goto fail;
    memcpy(metadata,p,(size_t)n);*length=(size_t)n;
   }
   if(!dbus_message_iter_next(&map))break;
  }
  index++;
 }while(dbus_message_iter_next(&args));
 if(index==3&&handles==5&&meta==1)return m;
fail:dbus_message_unref(m);return NULL;
}
#endif
