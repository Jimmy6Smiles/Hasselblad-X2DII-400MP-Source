/* Original-size baseline only. Do not compile into production or 407MP worker. */
#include "native_render_clone.h"
extern uint32_t dbus_message_get_serial(void*);
extern uint32_t dbus_message_get_reply_serial(void*);
extern const char *dbus_message_get_sender(void*);
extern const char *dbus_message_get_destination(void*);
extern const char *dbus_message_get_signature(void*);
extern int duss_hal_mem_alloc(void*,void**,uint32_t,uint32_t,uint32_t,uint32_t);
extern int duss_hal_mem_share(void*,uint64_t*);
extern int duss_hal_mem_map(void*,void**);
extern int duss_hal_mem_unmap(void*);
extern int duss_hal_mem_sync(void*,int);
#ifdef NATIVE_ENCODE
#include "native_jpeg_trial.h"
#endif
static int reply_match(void*c){
 const char*rule="type='method_return',sender='com.hasselblad.camera',eavesdrop='true'";
 void*m=dbus_message_new_method_call("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","AddMatch");
 if(!m)return 0;void*r=NULL;int ok=0;
 if(dbus_message_append_args(m,'s',&rule,0))r=dbus_connection_send_with_reply_and_block(c,m,3000,NULL);
 if(r){ok=dbus_message_get_type(r)==2;dbus_message_unref(r);}dbus_message_unref(m);return ok;
}
static uint64_t digest_yuv(const unsigned char*p){
 uint64_t h=UINT64_C(14695981039346656037);
 unsigned char cached_line[11656];
 for(unsigned plane=0;plane<2;plane++)for(unsigned y=0;y<8742;y++){
  const unsigned char*line=p+(plane?103251968u:0u)+(size_t)y*11776;
  memcpy(cached_line,line,sizeof cached_line);
  for(unsigned x=0;x<11656;x++){h^=cached_line[x];h*=UINT64_C(1099511628211);}
 }
 return h;
}
/* Separate Y/U/V samples, preserving UV pair alignment; no image bytes logged. */
static void sample_yuv(const unsigned char*p,unsigned char sample[3][4096]){
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  unsigned row=(2*y+1)*8742/128,col=((2*x+1)*11656/128)&~1u,i=y*64+x;
  sample[0][i]=p[(size_t)row*11776+col];
  sample[1][i]=p[103251968u+(size_t)row*11776+col];
  sample[2][i]=p[103251968u+(size_t)row*11776+col+1];
 }
}
static void sample_diff(unsigned char a[3][4096],unsigned char b[3][4096]){
 for(unsigned c=0;c<3;c++){
  unsigned sa=0,sb=0,ad=0,mx=0,nz=0,lo=255,hi=0;
  for(unsigned i=0;i<4096;i++){
   unsigned av=a[c][i],bv=b[c][i],d=av>bv?av-bv:bv-av;
   sa+=av;sb+=bv;ad+=d;if(d>mx)mx=d;nz+=d!=0;if(bv<lo)lo=bv;if(bv>hi)hi=bv;
  }
  printf("SAMPLE_CHANNEL %u ORIGINAL_SUM %u OWN_SUM %u ABS_SUM %u MAX_DIFF %u NONZERO %u OWN_RANGE %u %u\n",c,sa,sb,ad,mx,nz,lo,hi);
 }
}
static int repeat_native(void*c,void*event,void*dev,void*input,void*original,uint64_t offset){
 /* No guessing new geometry: this baseline is tied to the observed fixed output. */
 if(offset)return 50;
 const char*sender=dbus_message_get_sender(event);uint32_t serial=dbus_message_get_serial(event);
 if(!sender||!serial)return 50;
 double deadline=now()+15;int finished=0;
 while(!stop&&now()<deadline&&!finished){
  if(!dbus_connection_read_write(c,100))break;void*m;
  while((m=dbus_connection_pop_message(c))){
   const char*dest=dbus_message_get_destination(m);
   if(dbus_message_get_type(m)==2&&dest&&!strcmp(dest,sender)&&dbus_message_get_reply_serial(m)==serial)finished=1;
   dbus_message_unref(m);
  }
 }
 if(!finished||stop){puts("ORIGINAL_COMPLETION_NOT_CONFIRMED_NO_SUBMIT");return 51;}
 puts("ORIGINAL_COMPLETION_CONFIRMED");
 FILE*f=fopen("/proc/meminfo","r");unsigned long available=0;char line[256];
 if(f){while(fgets(line,sizeof line,f))if(sscanf(line,"MemAvailable: %lu kB",&available)==1)break;fclose(f);}
 /* Carveout capacity is separate from Linux MemAvailable. Keep both guards. */
 /* Debug filename suffix is not the allocation ID. Verify the observed mapping. */
 unsigned long long pool_free=0,pool_total=0,base=0,size=0;unsigned heap_id=0;int pool_id_ok=0;
 f=fopen("/sys/kernel/debug/ion/heaps/heap_addr","r");
 if(f){while(fgets(line,sizeof line,f))if(sscanf(line," %u %llx %llx",&heap_id,&base,&size)==3&&heap_id==2&&base==UINT64_C(0x130000000)&&size==UINT64_C(0xb0000000))pool_id_ok=1;fclose(f);}
 f=fopen("/sys/kernel/debug/ion/heaps/carveout_heap7","r");
 if(f){while(fgets(line,sizeof line,f)){sscanf(line," free size: %llu",&pool_free);sscanf(line," heap size: %llu",&pool_total);}fclose(f);}
 uint64_t required_pool=206503936u+268435456u;
#ifdef NATIVE_OWN_INPUT
 required_pool+=210513920u;
#endif
#ifdef NATIVE_ENCODE
 required_pool+=64u*1024u*1024u;
#endif
 if(!pool_id_ok||pool_total!=UINT64_C(0xb0000000)||available<128u*1024u||pool_free<required_pool){puts("RAM_OR_POOL_HEADROOM_REJECTED_NO_SUBMIT");return 52;}
 void*fresh=NULL,*map=NULL,*before_map=NULL,*request=NULL,*reply=NULL;int rc=53;uint64_t handle=0,original_hash=0;uint32_t actual=0;
#ifdef NATIVE_OWN_INPUT
 void*owned_input=NULL,*source_map=NULL,*input_map=NULL;
#endif
 unsigned char before[3][4096],after[3][4096];
 if(duss_hal_mem_map(original,&before_map)||!before_map||duss_hal_mem_sync(original,1))goto end;
 original_hash=digest_yuv(before_map);sample_yuv(before_map,before);if(duss_hal_mem_unmap(original))goto end;before_map=NULL;
 if(duss_hal_mem_alloc(dev,&fresh,206503936u,4096,2,0)||!fresh)goto end;
 if(duss_hal_mem_get_size(fresh,&actual)||actual<206503936u||duss_hal_mem_share(fresh,&handle)||!handle)goto end;
 /* The original sender can close its exported fd after completion. Re-export
  * our retained input reference; do not copy or modify its pixels. */
 uint64_t input_handle=0;
#ifdef NATIVE_OWN_INPUT
 /* Same native raster and metadata, but fully independent owned input.
  * This validates the input-buffer handoff needed by later merged tiles.
  * Original photograph and original buffer are never written. */
 if(duss_hal_mem_get_size(input,&actual)||actual!=210513920u||
    duss_hal_mem_alloc(dev,&owned_input,actual,4096,2,0)||!owned_input||
    duss_hal_mem_map(input,&source_map)||!source_map||duss_hal_mem_sync(input,1)||
    duss_hal_mem_map(owned_input,&input_map)||!input_map)goto end;
 for(unsigned y=0;y<8842;y++){
  if(stop)goto end;
  memcpy((unsigned char*)input_map+(size_t)y*23808,(const unsigned char*)source_map+(size_t)y*23808,23808);
 }
 memset((unsigned char*)input_map+210510336u,0,210513920u-210510336u);
 if(duss_hal_mem_unmap(input))goto end;source_map=NULL;
 if(duss_hal_mem_sync(owned_input,2)||duss_hal_mem_unmap(owned_input))goto end;input_map=NULL;
 if(duss_hal_mem_share(owned_input,&input_handle)||!input_handle)goto end;
 puts("OWN_INPUT_NATIVE_RASTER_COPIED_NO_NORMALIZATION");
#else
 if(duss_hal_mem_share(input,&input_handle)||!input_handle)goto end;
#endif
 request=clone_native(event,input_handle,handle);if(!request||stop)goto end;
 puts("OWN_OUTPUT_NATIVE_SUBMIT_ONCE");
 reply=dbus_connection_send_with_reply_and_block(c,request,20000,NULL);rc=54;
 if(!reply||dbus_message_get_type(reply)!=2||strcmp(dbus_message_get_signature(reply),""))goto end;
 puts("OWN_OUTPUT_NATIVE_COMPLETION");rc=55;
 if(duss_hal_mem_map(fresh,&map)||!map||duss_hal_mem_sync(fresh,1))goto end;
 uint64_t result=digest_yuv(map);
 sample_yuv(map,after);sample_diff(before,after);
 printf("NATIVE_YUV_ACTIVE_PIXEL_HASH %016llx ORIGINAL %016llx SAME %d\n",(unsigned long long)result,(unsigned long long)original_hash,result==original_hash);
 rc=0;
#ifdef NATIVE_ENCODE
 if(duss_hal_mem_unmap(fresh)){rc=56;goto end;}map=NULL;
 rc=native_jpeg_trial(dev,fresh);
#endif
end:
 if(map&&duss_hal_mem_unmap(fresh))rc=56;
 if(before_map&&duss_hal_mem_unmap(original))rc=56;
 if(reply)dbus_message_unref(reply);if(request)dbus_message_unref(request);
#ifdef NATIVE_OWN_INPUT
 if(source_map&&duss_hal_mem_unmap(input))rc=56;
 if(input_map&&duss_hal_mem_unmap(owned_input))rc=56;
 if(owned_input&&duss_hal_mem_free(owned_input))rc=56;
#endif
 if(fresh&&duss_hal_mem_free(fresh))rc=56;
 printf("REPEAT_EXIT %d NO_PHOTO_FILE_WRITES\n",rc);return rc;
}
