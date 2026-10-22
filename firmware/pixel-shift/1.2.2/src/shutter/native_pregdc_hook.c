/* Temporary init-trial process ONLY. Redirects two config reads to owned RAM
 * copies, and selects the private pre-GDC profile for RAW playback sessions.
 * Does not patch instructions, files, capture paths or security policy.
 * Production use is prohibited. Legacy build changes all RAW playback;
 * NATIVE_STATE_SCOPE instead binds the profile to a marked state-machine event.
 * Both still use diagnostic gates/nonces and an external lifetime guard. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#ifdef NATIVE_SHARED_DYNAMIC_HOST
#include "selected_host.h"
#endif
#include <dirent.h>
#include "native_lsc_register_transaction.h"
#include "native_request_token.h"
#ifdef NATIVE_JOB_RUNTIME
#include "native_job_lease.h"
static uint64_t job_nonce;
static int owned_token(const NativeRequestToken*t){NativeJobLease l;return t&&njl_read(&l)&&l.nonce==t->nonce;}
#endif
#ifndef NATIVE_RUNTIME_ROOT
#define NATIVE_RUNTIME_ROOT "/dev/x2d2-pregdc-trial"
#endif
#ifndef NATIVE_PROFILE_ROOT
#define NATIVE_PROFILE_ROOT NATIVE_RUNTIME_ROOT
#endif
#define ROOT NATIVE_RUNTIME_ROOT
static const char profile[]="x2d2_pixel_native_pregdc";
static const char original[]="frog_full_raw_to_yuv8_hiso_sp422_raw_pb";
static FILE*(*real_fopen)(const char*,const char*);
static int(*real_open)(const char*,int,...);
static int(*real_start)(void*,const void*,void**);
static int enabled,logfd=-1;
static int(*real_center)(const void*,unsigned,const void*,void*);
static atomic_uint center_records;
static int(*real_radial_load)(void*,const void*,void**);
static void*(*real_shading_pipe)(const void*,unsigned);
static atomic_uint radial_records,pipe_records;
static int(*real_rawpb_calc)(void*,void*,void*,void*,void*,unsigned);
static int(*real_metadata_vreg)(void*,void*,unsigned,void*);
static atomic_uint rawpb_records,metadata_records;
static _Thread_local unsigned private_start_depth;
static void*(*real_adj_create)(void*);
static int(*real_adj_release)(void*);
static atomic_uint lifecycle_records;
/* Passive request-lifetime tracing in the bounded diagnostic process only.
 * camera-service 0x205b38..0x205b4c calls session+0x10 with session+0x40,
 * input framebuffer and output framebuffer. The two input metadata pointers
 * are written at framebuffer+0x338/+0x350 by 0x205a50..0x205a64.
 * No metadata contents, pixel buffers, or camera files are changed here. */
typedef uintptr_t (*SubmitFrame)(void*,void*,void*);
static struct {void *session,*context;SubmitFrame original;} submit_slots[16];
static unsigned submit_count;
static pthread_mutex_t submit_mutex=PTHREAD_MUTEX_INITIALIZER;
static atomic_uint submit_records;
static int dynamic_experiment_gate(void);
typedef void*(*ContainerGet)(void*,unsigned,const char*,const char*,unsigned);
static ContainerGet container_get;
static int read_request_token(void*container,NativeRequestToken*out){
 if(!container||!out)return 0;
 if(!container_get)container_get=dlsym(RTLD_NEXT,"dcam_container_get_by_id_inner");
 if(!container_get)return 0;
 void*v=container_get(container,NRT_TAG,__FILE__,__func__,__LINE__);if(!v)return 0;
 uint32_t type,count,w[NRT_WORDS];void*data;
 memcpy(&type,v,4);memcpy(&count,(unsigned char*)v+4,4);memcpy(&data,(unsigned char*)v+8,8);
 if(type!=4||count!=NRT_WORDS||!data)return 0;
 memcpy(w,data,sizeof w);return nrt_decode(w,out);
}
#ifdef NATIVE_STATE_SCOPE
#include "native_state_scope.h"
#endif
static void trace_marker(const char*stage,void*container){
 if(!container||!dynamic_experiment_gate())return;
 NativeRequestToken token={0};int valid=read_request_token(container,&token);
 char line[180];int n=snprintf(line,sizeof line,"NATIVE_TOKEN %s container=%p valid=%d serial=%u x=%u y=%u\n",stage,container,valid,token.serial,token.x,token.y);
 if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
}
static atomic_uint container_records=256;
static void trace_container(const char*kind,void*a,void*b){
 if(!enabled||atomic_fetch_add(&container_records,1)>=256)return;
 char line[120];int n=snprintf(line,sizeof line,"NATIVE_CONTAINER_%s a=%p b=%p\n",kind,a,b);
 if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
}
void*dcam_container_clone_inner(void*source,const char*file,const char*function,unsigned line){
 typedef void*(*Fn)(void*,const char*,const char*,unsigned);static Fn next;
 if(!next)next=dlsym(RTLD_NEXT,"dcam_container_clone_inner");
 if(!next)return NULL;
 void*result=next(source,file,function,line);trace_container("CLONE",source,result);return result;
}
int dcam_container_copy_inner(void*target,void*source,const char*file,const char*function,unsigned line){
 typedef int(*Fn)(void*,void*,const char*,const char*,unsigned);static Fn next;
 if(!next)next=dlsym(RTLD_NEXT,"dcam_container_copy_inner");
 if(!next)return -1;
 int rc=next(target,source,file,function,line);if(!rc)trace_container("COPY",source,target);return rc;
}
#define TRACE_CONTAINER_TRANSFER(NAME,KIND) \
int NAME(void*target,void*source,const char*file,const char*function,unsigned line){ \
 typedef int(*Fn)(void*,void*,const char*,const char*,unsigned);static Fn next; \
 if(!next)next=dlsym(RTLD_NEXT,#NAME); \
 if(!next)return -1; \
 int rc=next(target,source,file,function,line);if(!rc)trace_container(KIND,source,target);return rc; \
}
TRACE_CONTAINER_TRANSFER(dcam_container_merge_inner,"MERGE")
TRACE_CONTAINER_TRANSFER(dcam_container_copy_no_override_inner,"COPY_KEEP")
static uintptr_t trace_submit(void*context,void*input,void*output){
 SubmitFrame next=NULL;unsigned slot=16;
 pthread_mutex_lock(&submit_mutex);
 for(unsigned i=0;i<submit_count;i++)if(submit_slots[i].context==context){next=submit_slots[i].original;slot=i;break;}
 pthread_mutex_unlock(&submit_mutex);
 if(!next)abort(); /* An unknown callback context cannot safely be forwarded. */
 atomic_store(&container_records,0);
 unsigned round=atomic_fetch_add(&submit_records,1);
 if(input&&round<32){
  void*a=NULL,*b=NULL;memcpy(&a,(unsigned char*)input+0x338,8);memcpy(&b,(unsigned char*)input+0x350,8);
  char line[180];int n=snprintf(line,sizeof line,"NATIVE_SUBMIT slot=%u context=%p metadata=%p,%p\n",slot,context,a,b);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
  /* Production-shaped token now originates in the worker's serialized
   * metadata, not by numbering whichever callback happened to run next. */
 }
 return next(context,input,output);
}
static void trace_session(void*session){
 if(!session)return;
 SubmitFrame next=NULL;void*context=NULL;Dl_info info={0};
 memcpy(&next,(unsigned char*)session+0x10,8);memcpy(&context,(unsigned char*)session+0x40,8);
 if(!next||!context||!dladdr((void*)next,&info)||!info.dli_fname||
    !strstr(info.dli_fname,"libdcam_"))return;
 pthread_mutex_lock(&submit_mutex);
 int duplicate=0;
 for(unsigned i=0;i<submit_count;i++)if(submit_slots[i].context==context||submit_slots[i].session==session)duplicate=1;
 /* A newly constructed native session may reuse the same address. Do not
  * suppress the trace merely because the address appeared in a prior request.
  * The original function/context pair is immutable for this diagnostic. */
 if(duplicate){
  for(unsigned i=0;i<submit_count;i++)if(submit_slots[i].context==context&&submit_slots[i].original==next){
   SubmitFrame replacement=trace_submit;memcpy((unsigned char*)session+0x10,&replacement,8);break;
  }
 }else if(submit_count<16){
  unsigned slot=submit_count++;submit_slots[slot].session=session;
  submit_slots[slot].context=context;submit_slots[slot].original=next;
  SubmitFrame replacement=trace_submit;memcpy((unsigned char*)session+0x10,&replacement,8);
 }
 pthread_mutex_unlock(&submit_mutex);
}
void*dcam_adj_create(void*configuration){
 if(!real_adj_create)real_adj_create=dlsym(RTLD_NEXT,"dcam_adj_create");
 if(!real_adj_create)return NULL;
 void*result=real_adj_create(configuration);
 if(enabled&&atomic_fetch_add(&lifecycle_records,1)<64){
  char line[120];int n=snprintf(line,sizeof line,"NATIVE_ADJ_CREATE context=%p private_depth=%u\n",result,private_start_depth);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 return result;
}
int dcam_adj_release(void*context){
 if(!real_adj_release)real_adj_release=dlsym(RTLD_NEXT,"dcam_adj_release");
 if(!real_adj_release)return -1;
 if(enabled&&atomic_fetch_add(&lifecycle_records,1)<64){
  char line[120];int n=snprintf(line,sizeof line,"NATIVE_ADJ_RELEASE context=%p\n",context);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 return real_adj_release(context);
}
static int(*read_vreg)(void*,unsigned,unsigned,uint32_t*);
static NlrWrite write_vreg;
static atomic_uint experiment_step;
static atomic_flag experiment_busy=ATOMIC_FLAG_INIT;
/* Bounded EXCLUSIVE experiment, NOT automatic production request binding.
 * GUI is stopped; only the controlled worker may issue the three requests.
 * Exact sequence0/2048/0 tests dynamic application without restarting the
 * native service. No correction outside the fresh explicit experiment gate. */
#ifdef NATIVE_NINE_TILE_TEST
#include "native_tile_plan.h"
#define NATIVE_TEST_LIMIT 9u
#define NATIVE_TEST_GATE "EXCLUSIVE_NO_GUI_NINE_TILE_TEST\n"
#else
#define NATIVE_TEST_LIMIT 3u
#define NATIVE_TEST_GATE "EXCLUSIVE_NO_GUI_THREE_REQUEST_TEST\n"
#endif
static int dynamic_experiment_gate(void){
 if(!enabled||!real_open)return 0;
 typedef int(*PropertyGet)(const char*,char*);
 PropertyGet property=dlsym(RTLD_DEFAULT,"__system_property_get");
 char state[128]={0};if(!property||property("init.svc.camera-gui",state)<=0)return 0;
 if(strcmp(state,"stopped")){
#ifdef NATIVE_JOB_RUNTIME
  if(strcmp(state,"running"))return 0;
#ifdef NATIVE_SHARED_RUNTIME
#ifndef NATIVE_SHARED_GUI_HOST
#error NATIVE_SHARED_GUI_HOST_required
#endif
#ifndef NATIVE_SHARED_GUI_PLUGIN
#error NATIVE_SHARED_GUI_PLUGIN_required
#endif
  /* The common runtime keeps the original camera-gui executable identity.
   * Require exactly that process plus both verified mapped components; never
   * depend on the retired replacement-GUI service or its DIRECT marker. */
  DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;unsigned found=0;int mapped=0;
#ifdef NATIVE_SHARED_DYNAMIC_HOST
  char selected[256],match[260];
  if(!cm_selected_host("gui","gui-coordinator.so",selected,sizeof selected)){closedir(d);return 0;}
  snprintf(match,sizeof match," %s\n",selected);
#endif
  while((e=readdir(d))){char*end;unsigned long pid=strtoul(e->d_name,&end,10);if(!pid||*end||pid>INT32_MAX)continue;
   char path[64],exe[160];snprintf(path,sizeof path,"/proc/%lu/exe",pid);ssize_t size=readlink(path,exe,sizeof exe-1);if(size<=0)continue;exe[size]=0;
   if(strcmp(exe,"/system/bin/camera-gui"))continue;found++;
   snprintf(path,sizeof path,"/proc/%lu/maps",pid);FILE*f=real_fopen(path,"r");if(!f)continue;
   char line[1024];int host=0,plugin=0;while(fgets(line,sizeof line,f)){
#ifdef NATIVE_SHARED_DYNAMIC_HOST
    if(strstr(line,match))host=1;
#else
    if(strstr(line," " NATIVE_SHARED_GUI_HOST "\n"))host=1;
#endif
    if(strstr(line," " NATIVE_SHARED_GUI_PLUGIN "\n"))plugin=1;
   }fclose(f);if(host&&plugin)mapped++;
  }closedir(d);if(found!=1||mapped!=1)return 0;
#else
  char mode[8]={0};int fd=real_open("/dev/x2d2-unified-boot/mode",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0)return 0;ssize_t n=read(fd,mode,sizeof mode);close(fd);
  if(n!=7||memcmp(mode,"DIRECT\n",7))return 0;
  DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;unsigned found=0;int factory=0;
  while((e=readdir(d))){char*end;unsigned long pid=strtoul(e->d_name,&end,10);if(!pid||*end||pid>INT32_MAX)continue;
   char path[64],exe[160];snprintf(path,sizeof path,"/proc/%lu/exe",pid);n=readlink(path,exe,sizeof exe-1);if(n<=0)continue;exe[n]=0;
   if(!strcmp(exe,"/system/x2d2-boot-v2/gui"))found++;
   if(!strcmp(exe,"/system/bin/camera-gui"))factory=1;
  }closedir(d);if(found!=1||factory)return 0;
#endif
#else
  return 0;
#endif
 }
#ifdef NATIVE_JOB_RUNTIME
 NativeJobLease lease;return njl_read(&lease);
#else
 int fd=real_open(ROOT"/dynamic.once",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 const char expected[]=NATIVE_TEST_GATE;char data[sizeof expected];struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==sizeof expected-1;
 time_t current=time(NULL);ok=ok&&current>=st.st_mtime&&current-st.st_mtime<=100;
 ok=ok&&read(fd,data,sizeof data)==sizeof expected-1&&!memcmp(data,expected,sizeof expected-1);close(fd);return ok;
#endif
}
static void dynamic_experiment(void*registers,unsigned count,void*metadata){
 if(!registers||count!=2||!metadata||!dynamic_experiment_gate())return;
 void*container;memcpy(&container,metadata,sizeof container);NativeRequestToken token;
 if(!read_request_token(container,&token)||token.serial>NATIVE_TEST_LIMIT)return;
#ifdef NATIVE_JOB_RUNTIME
 if(!owned_token(&token))return;
#else
 if(token.nonce!=UINT64_C(0x2159100511223344))return;
#endif
#ifdef NATIVE_STATE_SCOPE
 if(!nss_matches(&token))return;
#endif
#ifdef NATIVE_NINE_TILE_TEST
 NativeTile tile;if(!token.serial||!ntp_get(token.serial-1,&tile)||token.x!=tile.x||token.y!=tile.y)return;
#else
 if(!((token.x==token.y&&(token.x==0||token.x==2048))||
      (token.x==8192&&token.y==6144)||(token.x==11654&&token.y==8740)))return;
#endif
 if(atomic_flag_test_and_set(&experiment_busy))return;
#ifdef NATIVE_JOB_RUNTIME
 if(job_nonce!=token.nonce){job_nonce=token.nonce;atomic_store(&experiment_step,0);}
#endif
 unsigned step=atomic_load(&experiment_step);
#ifdef NATIVE_JOB_RUNTIME
 if(token.serial!=step+1){atomic_flag_clear(&experiment_busy);return;}
#endif
 if(step>=NATIVE_TEST_LIMIT){atomic_flag_clear(&experiment_busy);return;}
 if(!read_vreg)read_vreg=dlsym(RTLD_NEXT,"dcam_isp_vreg_get_reg");
 if(!write_vreg)write_vreg=dlsym(RTLD_NEXT,"dcam_isp_vreg_set_reg");
 void*windows[2];memcpy(windows,registers,sizeof windows);
 unsigned origin=token.x;int rc=nlr_apply_xy(windows,origin,token.y,read_vreg,write_vreg);
 if(rc==1)atomic_fetch_add(&experiment_step,1);
 if(rc<0)atomic_store(&experiment_step,NATIVE_TEST_LIMIT);
#ifdef NATIVE_JOB_RUNTIME
 {uint64_t ack[3]={token.nonce,token.serial,rc==1?1:0};
  int fd=real_open(ROOT"/job.ack",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd>=0){ssize_t written=write(fd,ack,sizeof ack);int synced=fsync(fd);close(fd);if(written!=sizeof ack||synced)atomic_store(&experiment_step,NATIVE_TEST_LIMIT);}}
#endif
 char line[160];int n=snprintf(line,sizeof line,"DYNAMIC_LSC_TOKEN_TEST serial=%u step=%u origin=%u rc=%d\n",token.serial,step,origin,rc);
 if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 atomic_flag_clear(&experiment_busy);
 if(rc==-2)abort(); /* Guard restores service; never continue partial correction. */
}
static void record_lsc_mirrors(const char*stage,void*registers,unsigned count){
 /* Array of pointers: native apply_single_algo_data0x36ad8c/0x36ade4.
  * IDs/4-channel bounds proved by inspect_native_lsc_registers.py. Public
  * get_reg0x54b90 uses mirror flags0, never hardware register I/O. */
 if(!registers||count!=2)return;
 if(!read_vreg)read_vreg=dlsym(RTLD_NEXT,"dcam_isp_vreg_get_reg");
 if(!read_vreg)return;
 static const unsigned ids[]={6122,6124,6126,6128,6136,6138,6142,6144};
 for(unsigned window=0;window<2;window++){
  void*vreg;memcpy(&vreg,(unsigned char*)registers+window*sizeof vreg,sizeof vreg);
  if(!vreg)continue;
  for(unsigned channel=0;channel<4;channel++){
   uint32_t value[8]={0};int rc=0;
   for(unsigned k=0;k<8;k++)rc|=read_vreg(vreg,ids[k],channel,&value[k]);
   char line[220];int n=snprintf(line,sizeof line,"LSC_MIRROR %s w=%u c=%u rc=%d centers=%u,%u,%u,%u diag=%u,%u,%u,%u\n",stage,window,channel,rc,value[0],value[1],value[2],value[3],value[4],value[5],value[6],value[7]);
   if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
  }
 }
}
/* libdcam_plugins_AIO.so call0x1821c8 passes six arguments;0x1821e4
 * passes four to metadata. Preserve arguments and return code. Mirror reads
 * are passive; the explicit GUI-stopped experiment gate alone enables the
 * separate, bounded mirror transaction after the native metadata call. */
int dcam_adj_calc_rawpb_single_frame(void*context,void*input,void*registers,
                                   void*calibration,void*output,unsigned count){
 if(!real_rawpb_calc)real_rawpb_calc=dlsym(RTLD_NEXT,"dcam_adj_calc_rawpb_single_frame");
 if(!real_rawpb_calc)return -1;
 int rc=real_rawpb_calc(context,input,registers,calibration,output,count);
 if(enabled&&atomic_fetch_add(&rawpb_records,1)<32){
  char line[140];int n=snprintf(line,sizeof line,"NATIVE_RAWPB_CALC context=%p count=%u rc=%d\n",context,count,rc);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
  if(rc==0&&logfd>=0)record_lsc_mirrors("calc",registers,count);
 }
 return rc;
}
int cam_update_metadata_to_vreg(void*context,void*registers,unsigned count,void*metadata){
 if(!real_metadata_vreg)real_metadata_vreg=dlsym(RTLD_NEXT,"cam_update_metadata_to_vreg");
 if(!real_metadata_vreg)return -1;
 int rc=real_metadata_vreg(context,registers,count,metadata);
 if(enabled&&rc==0&&metadata){
  void*pair[2];memcpy(pair,metadata,sizeof pair);
  trace_marker("callback0",pair[0]);trace_marker("callback1",pair[1]);
  char line[160];int n=snprintf(line,sizeof line,"NATIVE_METADATA_ID context=%p containers=%p,%p\n",context,pair[0],pair[1]);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 if(rc==0)dynamic_experiment(registers,count,metadata);
 if(enabled&&atomic_fetch_add(&metadata_records,1)<32){
  char line[100];int n=snprintf(line,sizeof line,"NATIVE_METADATA_VREG count=%u rc=%d\n",count,rc);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
  if(rc==0&&logfd>=0)record_lsc_mirrors("metadata",registers,count);
 }
 return rc;
}
/* Verified PLT call sites in librcam: f4eac (three arguments), f56b4
 * (two arguments, pointer return). Passive: no table reads or mutation.
 * Logging the result distinguishes the active loader from the unused public
 * center conversion entry; it does not establish a calibration layout. */
int fcali_load_shading_radial(void*loader,const void*key,void**result){
 if(!real_radial_load)real_radial_load=dlsym(RTLD_NEXT,"fcali_load_shading_radial");
 if(!real_radial_load)return -1;
 int rc=real_radial_load(loader,key,result);
 if(enabled&&atomic_fetch_add(&radial_records,1)<32){
  char line[96];int n=snprintf(line,sizeof line,"NATIVE_RADIAL_LOAD rc=%d\n",rc);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 return rc;
}
void*fcali_get_shading_pipe(const void*table,unsigned pipe){
 if(!real_shading_pipe)real_shading_pipe=dlsym(RTLD_NEXT,"fcali_get_shading_pipe");
 if(!real_shading_pipe)return NULL;
 void*result=real_shading_pipe(table,pipe);
 if(enabled&&atomic_fetch_add(&pipe_records,1)<64){
  char line[96];int n=snprintf(line,sizeof line,"NATIVE_SHADING_PIPE id=%u found=%d\n",pipe,result!=NULL);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 return result;
}
/* ABI from II libdcam_fcali.so 0x144468 wrapper /0x137ce8 common.
 * Only log scalar fields also read by the original function. No calibration
 * mutation, pointer following, or original-table export. */
int fcali_shading_convert_center_table(const void*geometry,unsigned window,const void*table,void*output){
 if(!real_center)real_center=dlsym(RTLD_NEXT,"fcali_shading_convert_center_table");
 if(!real_center)return -1;
 if(enabled&&geometry&&table&&output&&window<8&&atomic_fetch_add(&center_records,1)<16){
  uint32_t full[4],crop[6];char line[300];
  memcpy(full,(const unsigned char*)geometry+0x18,sizeof full);
  memcpy(crop,(const unsigned char*)geometry+0x3c+window*24,sizeof crop);
  int n=snprintf(line,sizeof line,"NATIVE_LSC_GEOMETRY window=%u norm=%u,%u center=%u,%u crop=%u,%u,%u,%u output=%u,%u\n",window,full[0],full[1],full[2],full[3],crop[0],crop[1],crop[2],crop[3],crop[4],crop[5]);
  if(logfd>=0&&n>0&&(size_t)n<sizeof line){ssize_t ignored=write(logfd,line,(size_t)n);(void)ignored;}
 }
 return real_center(geometry,window,table,output);
}
static const char*redirect(const char*p){
 if(!enabled||!p)return p;
 if(!strcmp(p,"/etc/iq/hb722_config.bin")||!strcmp(p,"/system/etc/iq/hb722_config.bin"))return NATIVE_PROFILE_ROOT"/diagnostic-iq.bin";
 if(!strcmp(p,"/etc/imx861bqr_hb722.conf")||!strcmp(p,"/system/etc/imx861bqr_hb722.conf"))return NATIVE_PROFILE_ROOT"/candidate.conf";
 if(!strcmp(p,"/etc/imx861bqr_hb722.sp")||!strcmp(p,"/system/etc/imx861bqr_hb722.sp"))return NATIVE_PROFILE_ROOT"/candidate.sp";
 return p;
}
static void record(const char*s){if(logfd>=0){ssize_t ignored=write(logfd,s,strlen(s));(void)ignored;}}
#ifdef NATIVE_FACTORY_GRID
#include "native_factory_grid_hook.h"
#endif
static void record_redirect(const char*p){record(strstr(p,".conf")?"REDIRECT_CONF\n":strstr(p,"diagnostic-iq.bin")?"REDIRECT_IQ\n":"REDIRECT_SP\n");}
__attribute__((constructor)) static void init(void){
 real_fopen=dlsym(RTLD_NEXT,"fopen");real_open=dlsym(RTLD_NEXT,"open");
 real_start=dlsym(RTLD_NEXT,"cam_cap_eng_start_raw_reprocess");
 if(!real_fopen||!real_open||!real_start)return;
 char exe[128];ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1);if(n<=0)return;exe[n]=0;
 if(strcmp(exe,"/system/bin/camera-service"))return;
 /* Authorization and calibration inputs are immutable feature-owned files.
  * Runtime leases, acknowledgements and diagnostics remain in feature-owned
  * RAM.  This lets the hook be installed while camera-service starts, before
  * the feature backend has created its per-boot runtime directory. */
 int fd=real_open(NATIVE_PROFILE_ROOT"/authorized.once",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
#ifdef NATIVE_JOB_RUNTIME
 const char text[]="PREGDC_JOB_SCOPED_NATIVE_COLOR_V1\n";
#else
 const char text[]="PREGDC_RAM_DIAGNOSTIC_NO_CAPTURE\n";
#endif
 char buf[sizeof text];struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==sizeof text-1&&read(fd,buf,sizeof buf)==sizeof text-1&&!memcmp(buf,text,sizeof text-1);close(fd);
 if(!ok)return;
 /* Logging is diagnostic only.  A missing runtime directory at constructor
  * time must not disable the already authorized immutable profile. */
 logfd=real_open(ROOT"/hook.log",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
#ifdef NATIVE_STATE_SCOPE
 if(!nss_install()){record("NATIVE_STATE_SCOPE_INSTALL_FAILED_NO_REDIRECT\n");return;}
 record("NATIVE_STATE_SCOPE_RAM_POINTERS_INSTALLED\n");
#endif
 enabled=1;record("PREGDC_HOOK_READY_TEMPORARY_PROCESS_ONLY\n");
}
FILE*fopen(const char*p,const char*mode){
 if(!real_fopen)real_fopen=dlsym(RTLD_NEXT,"fopen");
 if(!real_fopen){errno=ENOSYS;return NULL;}
 const char*q=redirect(p);
 if(q!=p){if(strcmp(mode,"r")&&strcmp(mode,"rb")){errno=EACCES;return NULL;}record_redirect(q);}
 return real_fopen(q,mode);
}
int open(const char*p,int flags,...){
 mode_t mode=0;if((flags&O_CREAT)||(flags&O_TMPFILE)==O_TMPFILE){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
 if(!real_open)real_open=dlsym(RTLD_NEXT,"open");
 if(!real_open){errno=ENOSYS;return -1;}
 const char*q=redirect(p);
 if(q!=p){if((flags&O_ACCMODE)!=O_RDONLY||(flags&(O_CREAT|O_TRUNC))){errno=EACCES;return -1;}record_redirect(q);}
 return real_open(q,flags,mode);
}
int __open_2(const char*p,int flags){if((flags&O_CREAT)||(flags&O_TMPFILE)==O_TMPFILE){errno=EINVAL;return -1;}return open(p,flags);}
int cam_cap_eng_start_raw_reprocess(void*engine,const void*parameters,void**session){
 if(!real_start)real_start=dlsym(RTLD_NEXT,"cam_cap_eng_start_raw_reprocess");
 if(!real_start)return -1;
 if(!enabled||!parameters)return real_start(engine,parameters,session);
 /* camera-service SHA51b9...: 0x2920e8 clears0x54d0 bytes; 0x29211c
  * writes profile pointer at +8 before call0x29214c. Original stack input
  * lifetime proves the framework must not retain the parameter pointer. */
 const char*name=NULL;memcpy(&name,(const unsigned char*)parameters+8,sizeof name);
 if(!name||strcmp(name,original))return real_start(engine,parameters,session);
#ifdef NATIVE_STATE_SCOPE
 NativeRequestToken active={0};
 if(!nss_snapshot(&active)||
#ifdef NATIVE_JOB_RUNTIME
    !owned_token(&active)||
#else
    active.nonce!=UINT64_C(0x2159100511223344)||
#endif
    (active.serial>NATIVE_TEST_LIMIT&&active.serial!=UINT32_C(0x80000002))||!dynamic_experiment_gate()){
  record("UNOWNED_PROFILE_UNCHANGED\n");return real_start(engine,parameters,session);
 }
#endif
 unsigned char*copy=malloc(0x54d0);if(!copy)return -1;
 memcpy(copy,parameters,0x54d0);const char*selected=profile;memcpy(copy+8,&selected,sizeof selected);
 record("PRIVATE_PREGDC_PROFILE_SELECTED\n");private_start_depth++;
 int rc=real_start(engine,copy,session);private_start_depth--;free(copy);
#ifndef NATIVE_JOB_RUNTIME
 if(rc==0&&session)trace_session(*session);
#endif
 record(rc?"PRIVATE_PREGDC_START_FAILED\n":"PRIVATE_PREGDC_START_RETURNED_SUCCESS\n");return rc;
}
