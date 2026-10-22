/* Import the two buffers announced by one native render request, query actual
 * allocation sizes, then release our references. Never map/read/write pixels. */
extern int duss_hal_initialize(void*);
extern int duss_hal_deinitialize(void);
extern int duss_hal_attach_ion_mem(const char*,void**);
extern int duss_hal_detach_ion_mem(void*);
extern int duss_hal_device_open(const char*,void*,void**);
extern int duss_hal_device_start(void*,void*);
extern int duss_hal_device_stop(void*,void*);
extern int duss_hal_device_close(void*);
extern int duss_hal_mem_import(void*,void**,uint64_t);
extern int duss_hal_mem_free(void*);
extern int duss_hal_mem_get_size(void*,uint32_t*);
#ifdef NATIVE_ENCODE
extern int duss_hal_attach_verislcon_ienc(const char*,void**);
extern int duss_hal_detach_verislcon_ienc(void*);
#endif
#ifdef NATIVE_REPEAT
#include "native_render_repeat.h"
static int baseline_geometry(void*m){
 static const char*keys[]={"WIDTH","HEIGHT","STRIDE","FORMAT","SRC_FORMAT","CROP_X","CROP_Y","CROP_W","CROP_H","PLANE_0_OFFSET","PLANE_0_CONTENT_SIZE"};
 static const int32_t wanted[2][11]={{11836,8842,11904,1,255,0,0,11836,8842,0,210510336},
 {11656,8742,11776,6,255,0,0,11656,8742,0,102945792}};
 Iter args={0};unsigned index=0;if(!dbus_message_iter_init(m,&args))return 0;
 do{
  if(index>=3)return 0;
  if(index!=1){unsigned seen=0;Iter map={0};dbus_message_iter_recurse(&args,&map);
   while(dbus_message_iter_get_arg_type(&map)){
    Iter pair={0},v={0};const char*k=NULL;dbus_message_iter_recurse(&map,&pair);
    dbus_message_iter_get_basic(&pair,&k);if(!k||!dbus_message_iter_next(&pair))return 0;
    dbus_message_iter_recurse(&pair,&v);
    for(unsigned j=0;j<11;j++)if(!strcmp(k,keys[j])){
     int32_t n=0;if((seen&(1u<<j))||dbus_message_iter_get_arg_type(&v)!='i')return 0;
     dbus_message_iter_get_basic(&v,&n);if(n!=wanted[index/2][j])return 0;seen|=1u<<j;
    }
    if(!dbus_message_iter_next(&map))break;
   }
   if(seen!=2047u)return 0;
  }
  index++;
 }while(dbus_message_iter_next(&args));
 return index==3;
}
#endif
static int import_sizes(void*c,void*m){
#ifndef NATIVE_REPEAT
 (void)c;
#endif
 uint64_t handles[2]={0},offset[2]={0},end[2]={0};unsigned a=0;
 Iter args={0};if(!dbus_message_iter_init(m,&args))return 40;
 do{
  if(a>2||dbus_message_iter_get_arg_type(&args)!='a')return 40;
  if(a!=1){
   unsigned idx=a/2,seen=0;uint32_t po[2]={0},pc[2]={0};Iter map={0};dbus_message_iter_recurse(&args,&map);
   while(dbus_message_iter_get_arg_type(&map)){
    Iter pair={0},v={0};const char*k=NULL;dbus_message_iter_recurse(&map,&pair);dbus_message_iter_get_basic(&pair,&k);
    if(!k||!dbus_message_iter_next(&pair))return 40;dbus_message_iter_recurse(&pair,&v);int typ=dbus_message_iter_get_arg_type(&v);
    if(!strcmp(k,"SHARE_HANDLE")){if(typ!='t'||seen++)return 40;dbus_message_iter_get_basic(&v,handles+idx);}
    else if(!strcmp(k,"DUSS_OFFSET")){if(typ!='t')return 40;dbus_message_iter_get_basic(&v,offset+idx);}
    else for(unsigned p=0;p<2;p++){
     const char*o=p?"PLANE_1_OFFSET":"PLANE_0_OFFSET";const char*c=p?"PLANE_1_CONTENT_SIZE":"PLANE_0_CONTENT_SIZE";
     if(!strcmp(k,o)||!strcmp(k,c)){
      int32_t n;if(typ!='i')return 40;dbus_message_iter_get_basic(&v,&n);if(n<0)return 40;
      if(!strcmp(k,o))po[p]=(uint32_t)n;else pc[p]=(uint32_t)n;
     }
    }
    if(!dbus_message_iter_next(&map))break;
   }
   if(seen!=1||!handles[idx]||offset[idx]>UINT32_MAX)return 40;
   for(unsigned p=0;p<2;p++)if(pc[p]&&((uint64_t)po[p]+pc[p]>end[idx]))end[idx]=(uint64_t)po[p]+pc[p];
   end[idx]+=offset[idx];if(!end[idx]||end[idx]>UINT32_MAX)return 40;
  }
  a++;
 }while(dbus_message_iter_next(&args));
 if(a!=3)return 40;
#ifdef NATIVE_REPEAT
 if(offset[0]||offset[1]||!baseline_geometry(m))return 45;
#endif
 struct Module{const char*name;int(*attach)(const char*,void**);int(*detach)(void*);void*handle;};
 struct Module modules[]={{"/dev/ion",duss_hal_attach_ion_mem,duss_hal_detach_ion_mem,NULL},
#ifdef NATIVE_ENCODE
 {"/dev/ienc0",duss_hal_attach_verislcon_ienc,duss_hal_detach_verislcon_ienc,NULL},
#endif
 {0}};
 void*dev=NULL,*buffers[2]={NULL,NULL};int initialized=0,started=0,rc=41;uint32_t config=0;
 if(duss_hal_initialize(modules))goto end;initialized=1;
 if(duss_hal_device_open("/dev/ion",&config,&dev))goto end;
 if(duss_hal_device_start(dev,&config))goto end;started=1;
 for(unsigned i=0;i<2;i++){
  uint32_t bytes=0;rc=42;
  if(duss_hal_mem_import(dev,&buffers[i],handles[i])||!buffers[i]||duss_hal_mem_get_size(buffers[i],&bytes))goto end;
  printf("BUFFER %u ACTUAL_BYTES %u REQUIRED_END %llu FITS %d\n",i,bytes,(unsigned long long)end[i],end[i]<=bytes);
  if(end[i]>bytes){rc=43;goto end;}
 }
 rc=0;
#ifdef NATIVE_REPEAT
 if(end[0]!=210510336u||end[1]!=206197760u)rc=45;
 else rc=repeat_native(c,m,dev,buffers[0],buffers[1],offset[1]);
#endif
end:
 for(unsigned i=0;i<2;i++)if(buffers[i]&&duss_hal_mem_free(buffers[i]))rc=44;
 if(started&&duss_hal_device_stop(dev,NULL))rc=44;
 if(dev&&duss_hal_device_close(dev))rc=44;
 if(initialized&&duss_hal_deinitialize())rc=44;
#ifdef NATIVE_REPEAT
 printf("IMPORT_CHECK_EXIT %d NO_ORIGINAL_BUFFER_WRITES\n",rc);
#else
 printf("IMPORT_CHECK_EXIT %d NO_PIXEL_ACCESS\n",rc);
#endif
 return rc;
}
