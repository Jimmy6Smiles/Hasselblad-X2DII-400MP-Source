/* One experimental 100MP window of the actual 407MP merged raster.
 * Fixed retained sources only. No capture, album, source writes or publication.
 * Native processing here does NOT establish correct 407MP spatial calibration. */
#define _GNU_SOURCE
#define main unused_worker_main
#include "overlap_six_worker.c"
#undef main
#include "native_merge_rows.h"
#include "native_cache_rows.h"
#include "native_tile_window.h"
#ifdef NATIVE_JOB_RUNTIME
#include "native_job_paths.h"
static NativeJobPaths job_paths;
#endif
#ifdef NATIVE_CACHE_INPUT
#include "native_cache_rows.h"
#endif
#ifdef NATIVE_FULL_PREVIEW
#include "native_preview_assembly.h"
#ifdef NATIVE_FACTORY_GRID
#if !defined(NATIVE_JOB_RUNTIME) || !defined(NATIVE_FACTORY_CHROMA_SITING)
#error Factory grid candidate requires runtime identity and explicit chroma-siting test
#endif
#include "native_factory_grid_bundle.h"
#include "native_preview_dewarp.h"
#ifdef NATIVE_FACTORY_REFERENCE_PREVIEW
#ifndef NATIVE_FACTORY_REFERENCE_ONLY
#error Reference preview must never run the merged tile path
#endif
#include "native_reference_reduce.h"
#endif
/* Hardware80, not90, matches all128 factory embedded-preview quantizers;
 * verified with synthetic camera encodes20261006-013720. Candidate only. */
#define NATIVE_JPEG_QUALITY 80
static int factory_grid_load(NativeFactoryMesh*m,uint64_t nonce){
 char path[160];snprintf(path,sizeof path,"/dev/x2d2-pregdc-trial/grid-%016llx.bin",(unsigned long long)nonce);
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&!(st.st_mode&0077)&&st.st_size>=NFGB_HEADER&&st.st_size<=NFGB_LIMIT;
 unsigned char*p=ok?malloc((size_t)st.st_size):NULL;size_t at=0;
 if(p)while(at<(size_t)st.st_size){ssize_t n=read(fd,p+at,(size_t)st.st_size-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(size_t)n;}
 ok=p&&at==(size_t)st.st_size&&nfgb_decode(m,nonce,p,at);
 if(ok)printf("FACTORY_GRID_LOADED nonce=%016llx crc=%08x bytes=%zu\n",(unsigned long long)nonce,nfm_u32(p+36),at);
 free(p);if(close(fd))ok=0;
 /* Keep the nonce-bound grid for the separate whole-image disk warp. */ return ok;
}
#endif
static int preview_cancel(void*unused){(void)unused;return cancelled!=0;}
static int preview_cached_sink(void*context,unsigned y,unsigned x,unsigned n,const unsigned char*luma,const unsigned char*uv){
 if(cancelled||n>NTP_TILE_WIDTH)return 0;
 unsigned char yrow[NTP_TILE_WIDTH],uvrow[NTP_TILE_WIDTH];
 /* ION mapping may be uncached: bulk copy once, accumulate in cached RAM. */
 memcpy(yrow,luma,n);memcpy(uvrow,uv,n);
 return !cancelled&&npa_sink(context,y,x,n,yrow,uvrow);
}
static int native_verify_jpeg(const unsigned char*data,unsigned bytes,unsigned width,unsigned height){
#ifdef NATIVE_JOB_RUNTIME
 if(width!=1920||height!=1440||!bytes||bytes>8u*1024*1024)return 0;
 const char*path=job_paths.preview;
#else
 char path[256];const char prefix[]="/dev/x2d2-native-observe-";
 ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-16);if(n<=0)return 0;path[n]=0;
 if(strncmp(path,prefix,sizeof(prefix)-1)||width!=1920||height!=1440||!bytes||bytes>16u*1024*1024)return 0;
 char*suffix=path+sizeof(prefix)-1;
 for(unsigned i=0;i<6;i++)if(suffix[i]<'0'||suffix[i]>'9')return 0;
 if(strcmp(suffix+6,"/observer"))return 0;strcpy(suffix+6,"/preview.jpg");
#endif
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 unsigned offset=0;while(offset<bytes){ssize_t w=write(fd,data+offset,bytes-offset);if(w<0&&errno==EINTR)continue;if(w<=0){close(fd);return 0;}offset+=(unsigned)w;}
 int synced=fsync(fd),closed=close(fd);if(synced||closed)return 0;printf("NATIVE_PREVIEW_EXPORTED %u DECODE_REQUIRED_BEFORE_PUBLICATION\n",bytes);return 1;
}
#define NATIVE_JPEG_VERIFY
#endif
#define NATIVE_IMPORT_SIZES
#define NATIVE_REPEAT
#define NATIVE_ENCODE
#define main unused_observer_main
#include "native_render_observer.c"
#undef main
#include "native_template_load.h"
#include "native_request_token.h"
#ifdef NATIVE_JOB_RUNTIME
#include "native_job_metadata.h"
#include "native_job_inputs.h"
#include "native_render_job.h"
#include "native_service_quiesce.h"
#include <poll.h>
#include "native_view_control.h"
#endif
#include <ctype.h>
static void tile_cancel(int s){on_signal(s);stopped(s);}
#if defined(NATIVE_COLOR_REFERENCE) || defined(NATIVE_FULL_PREVIEW)
#ifndef NATIVE_REFERENCE_X
#define NATIVE_REFERENCE_X 0
#define NATIVE_REFERENCE_Y 0
#endif
static int exclusive_session(void){
 extern int __system_property_get(const char*,char*);char state[128];
 const char*names[]={"init.svc.camera-gui","init.svc.camera-service","init.svc.x2d2-capture-trial","init.svc.x2d2-trial-guard"};
 const char*values[]={"stopped","stopped","running","running"};
 for(unsigned i=0;i<4;i++)if(__system_property_get(names[i],state)<=0||strcmp(state,values[i]))return 0;
 return 1;
}
static int stop_view_before_render(void*connection){
 if(cancelled||!exclusive_session())return 0;
 void*m=dbus_message_new_method_call("com.hasselblad.camera","/camera","com.hasselblad.camera","set_live_view");
 if(!m)return 0;Iter it={0};int off=0;dbus_message_iter_init_append(m,&it);
 if(!dbus_message_iter_append_basic(&it,'b',&off)){dbus_message_unref(m);return 0;}
 void*r=dbus_connection_send_with_reply_and_block(connection,m,5000,NULL);dbus_message_unref(m);
 int ok=r&&dbus_message_get_type(r)==2&&!strcmp(dbus_message_get_signature(r),"");
 if(r)dbus_message_unref(r);return ok&&!cancelled&&exclusive_session();
}
#endif
static void overlap_samples(const unsigned char*p,unsigned origin,unsigned char out[3][4096]){
 /* Same global locations, at least 128 pixels inside the shared rectangle. */
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
#ifdef NATIVE_COLOR_REFERENCE
  unsigned gx=(256+(2*x+1)*(11656-512)/128)&~3u,gy=(256+(2*y+1)*(8742-512)/128)&~3u;
  unsigned col=origin?(gx+NATIVE_REFERENCE_X)/2:gx,line=origin?(gy+NATIVE_REFERENCE_Y)/2:gy,index=y*64+x;
#elif defined(NATIVE_EDGE_TEST)
  unsigned gx=(11782+(2*x+1)*(19720-11782)/128)&~1u,gy=8868+(2*y+1)*(14758-8868)/128;
  unsigned col=gx-(origin?11654:8192),line=gy-(origin?8740:6144),index=y*64+x;
#else
  unsigned gx=(2176+(2*x+1)*(11656-2304)/128)&~1u,gy=2176+(2*y+1)*(8742-2304)/128;
  unsigned col=gx-origin,line=gy-origin,index=y*64+x;
#endif
  out[0][index]=p[(size_t)line*11776+col];
  out[1][index]=p[103251968u+(size_t)line*11776+(col&~1u)];
  out[2][index]=p[103251968u+(size_t)line*11776+(col&~1u)+1];
 }
}
static uint64_t overlap_raw_hash(const uint16_t*p,unsigned origin){
 uint64_t h=UINT64_C(14695981039346656037);
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  unsigned gx=(2176+(2*x+1)*(11656-2304)/128)&~1u,gy=2176+(2*y+1)*(8742-2304)/128;
#ifdef NATIVE_EDGE_TEST
  gx=(11782+(2*x+1)*(19720-11782)/128)&~1u;gy=8868+(2*y+1)*(14758-8868)/128;
  h^=p[(size_t)(gy-(origin?8740:6144)+96)*11904+gx-(origin?11654:8192)+129];
#else
  h^=p[(size_t)(gy-origin+96)*11904+gx-origin+129];
#endif
  h*=UINT64_C(1099511628211);
 }
 return h;
}
/* Matching luminance at candidate offsets helps distinguish placement from
 * color differences. Diagnostic only, NOT a warp correction or acceptance test. */
static void shift_grid(const unsigned char*p,unsigned origin,unsigned char*patches,unsigned char*centers){
 for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
  unsigned gx=(2176+(2*x+1)*(11656-2304)/64)&~1u,gy=2176+(2*y+1)*(8742-2304)/64,i=y*32+x;
  if(!origin)for(int dy=-16;dy<=16;dy++)for(int dx=-16;dx<=16;dx++)
   patches[i*1089+(dy+16)*33+dx+16]=p[(size_t)(gy+dy)*11776+gx+dx];
  else centers[i]=p[(size_t)(gy-origin)*11776+gx-origin];
 }
}
static void report_shift(const unsigned char*patches,const unsigned char*centers){
 for(unsigned region=0;region<5;region++){
  uint64_t best=UINT64_MAX,zero=0;int bx=0,by=0;unsigned count=0;
  for(int dy=-16;dy<=16;dy++)for(int dx=-16;dx<=16;dx++){
   uint64_t sum=0;unsigned n=0;
   for(unsigned i=0;i<1024;i++){
    unsigned quadrant=(i/32>=16)*2+(i%32>=16);
    if(region&&quadrant!=region-1)continue;
    int delta=(int)patches[i*1089+(dy+16)*33+dx+16]-centers[i];sum+=delta<0?-delta:delta;n++;
   }
   if(!dx&&!dy)zero=sum;
   if(sum<best){best=sum;bx=dx;by=dy;}count=n;
  }
  printf("WINDOW_SHIFT_SEARCH REGION %u COUNT %u ZERO_SAD %llu BEST_SAD %llu DX %d DY %d\n",region,count,(unsigned long long)zero,(unsigned long long)best,bx,by);
 }
}
static int tile_pool_ok(void){
 FILE*f;char line[256];unsigned long available=0;unsigned long long free_bytes=0,total=0,base=0,size=0;unsigned id=0;int found=0;
 f=fopen("/proc/meminfo","r");if(f){while(fgets(line,sizeof line,f))if(sscanf(line,"MemAvailable: %lu kB",&available)==1)break;fclose(f);}
 f=fopen("/sys/kernel/debug/ion/heaps/heap_addr","r");if(f){while(fgets(line,sizeof line,f))if(sscanf(line," %u %llx %llx",&id,&base,&size)==3&&id==2&&base==UINT64_C(0x130000000)&&size==UINT64_C(0xb0000000))found=1;fclose(f);}
 f=fopen("/sys/kernel/debug/ion/heaps/carveout_heap7","r");if(f){while(fgets(line,sizeof line,f)){sscanf(line," free size: %llu",&free_bytes);sscanf(line," heap size: %llu",&total);}fclose(f);}
 return found&&total==UINT64_C(0xb0000000)&&available>=128*1024&&free_bytes>=210513920ull+206503936+67108864+536870912;
}
int main(int argc,char**argv){
#ifdef NATIVE_JOB_RUNTIME
 NativeJobInputs job_sources;NativeServiceIdentity service;
 if(!nji_args(&job_paths,argc,argv)||!nji_open(&job_sources,&job_paths)||!nsq_identify(&service))return 2;
 /* This marker is installed only by the updated supervisor, never by the
  * worker. Old supervisors kill children before stopping DMA; reject them. */
 if(access("/dev/x2d2-integrated-v1/native-drain-capable",F_OK))return 2;
 char root[256],path[320];strcpy(root,job_paths.work);
 uint64_t nonce=job_paths.nonce;NativeRenderJob render={0};NativeJobLease lease={0};
 NativeViewScope view={0};int view_owned=0,lease_owned=0,view_fd=-1,unknown=0;
 if(!nrj_init(&render,nonce,njl_clock(),240000))return 2;
#ifdef NATIVE_FACTORY_GRID
 if(!nrj_require_reference(&render))return 2;
#endif
#else
 if(argc!=2||strcmp(argv[1],"--check-merged-tile"))return 2;
 char root[256],path[320],actual_path[256],expected[256];const char prefix[]="/dev/x2d2-native-observe-";
 ssize_t n=readlink("/proc/self/exe",root,sizeof root-1);if(n<=0)return 2;root[n]=0;
 if(strncmp(root,prefix,sizeof prefix-1))return 2;char*suffix=root+sizeof prefix-1;
 for(unsigned i=0;i<6;i++)if(!isdigit((unsigned char)suffix[i]))return 2;
 if(strcmp(suffix+6,"/observer"))return 2;suffix[6]=0;
 uint64_t nonce=UINT64_C(0x2159100511223344);
 (void)nonce;
#endif
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,tile_cancel);signal(SIGINT,tile_cancel);
 int rc=1,initialized=0,started=0;void*c=NULL,*event=NULL,*request=NULL,*reply=NULL,*ion=NULL,*input=NULL,*output=NULL,*mapped=NULL;
 NativeMergeRows rows={0};uint16_t*row=NULL;unsigned char metadata[32768];size_t length=0;
 unsigned char*patches=NULL,*centers=NULL;
#ifdef NATIVE_FULL_PREVIEW
 NativePreview preview={0};NativeAssembly assembly={0};void*preview_mem=NULL;
#ifdef NATIVE_FACTORY_GRID
 NativeFactoryMesh factory_mesh={0};
#endif
#endif
#ifdef NATIVE_CACHE_INPUT
 NativeCacheRows cache={.fd=-1};
#ifdef NATIVE_MERGED_CONTAINER_INPUT
 char merged_path[256];snprintf(merged_path,sizeof merged_path,"%s/output.3FR",job_paths.stage);
 if(!ncr_open_container(&cache,merged_path,&cancelled))goto end;
#else
 if(!ncr_open(&cache,
#ifdef NATIVE_JOB_RUNTIME
 job_paths.cache,
#else
 "/mnt/media_rw/cfe/x2d2-native-color-225937/native.u16.partial",
#endif
 &cancelled))goto end;
#endif
#endif
#ifdef NATIVE_FULL_PREVIEW
#ifndef NATIVE_FACTORY_REFERENCE_ONLY
 if(!tile_pool_ok()||!npa_init(&preview,1920,1440)||!nia_init(&assembly,nonce))goto end;
#endif
#endif
#ifndef NATIVE_JOB_RUNTIME
 for(unsigned i=0;i<6;i++){
  snprintf(path,sizeof path,"%s/input%u.3fr",root,i);snprintf(expected,sizeof expected,"/mnt/media_rw/cfe/DCIM/999HASBL/B%07u.3FR",2591+i);
  n=readlink(path,actual_path,sizeof actual_path-1);if(n<=0)goto end;actual_path[n]=0;if(strcmp(actual_path,expected))goto end;
 }
 snprintf(path,sizeof path,"%s/template.dbus",root);event=nt_load(path,metadata,sizeof metadata,&length);
 if(!event||!baseline_geometry(event)||!tile_pool_ok())goto end;
 NativeDcamColor color={{175168,65536,65536,65536,128720,65536},{16384,16659,16624,16384},4096,64762};
 if(!nd_color_patch(metadata,length,&color))goto end;
 NativeRawRange target={{4096,4096,4096,4096},64762};
#else
 snprintf(path,sizeof path,"%s/first-frame.dbus",root);event=nt_load(path,metadata,sizeof metadata,&length);
 NativeRawRange target;
 if(!event||!baseline_geometry(event)||!nji_description(event,job_paths.first)||!njm_range(metadata,length,&target)||!tile_pool_ok())goto end;
#ifdef NATIVE_MERGED_CONTAINER_INPUT
 for(unsigned i=0;i<4;i++)if(target.black[i]!=cache.black)goto end;
 if(target.white!=cache.white)goto end;
 puts("NATIVE_RENDER_USES_EXACT_MERGED_CONTAINER_NO_RANGE_REMAP");
#endif
 if(!nvs_begin(&view,nvc_get()))goto end;
 snprintf(path,sizeof path,"%s/native-view.journal",root);
 view_fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(view_fd<0||dprintf(view_fd,"VIEW_INTENT original=%d\n",view.original)<0||fsync(view_fd))goto end;
 view_owned=1;
 if((view.original&&!nvc_set(0))||!nvs_off(&view,nvc_get()))goto end;
 if(!njl_create(&lease,nonce))goto end;lease_owned=1;
#endif
 row=malloc(23310u*2);if(!row)goto end;
 struct Module{const char*name;int(*attach)(const char*,void**);int(*detach)(void*);void*handle;};
 struct Module modules[]={{"/dev/ion",duss_hal_attach_ion_mem,duss_hal_detach_ion_mem,NULL},{"/dev/ienc0",duss_hal_attach_verislcon_ienc,duss_hal_detach_verislcon_ienc,NULL},{0}};
 uint32_t config=0,size=0;uint64_t ih=0,oh=0;
 if(duss_hal_initialize(modules))goto end;initialized=1;
 if(duss_hal_device_open("/dev/ion",&config,&ion)||!ion||duss_hal_device_start(ion,&config))goto end;started=1;
 if(duss_hal_mem_alloc(ion,&input,210513920,4096,2,0)||!input||duss_hal_mem_get_size(input,&size)||size!=210513920)goto end;
#ifdef NATIVE_FULL_PREVIEW
 unsigned rounds=NTP_COUNT;
#ifdef NATIVE_FACTORY_GRID
 rounds++;
#ifdef NATIVE_FACTORY_REFERENCE_ONLY
 rounds=1;
#endif
#endif
#elif defined(NATIVE_TILE_OVERLAP)
 unsigned char samples[3][3][4096];unsigned rounds=3;
 patches=malloc(1024u*1089);centers=malloc(1024);if(!patches||!centers)goto end;
#else
 unsigned rounds=1;
#endif
 for(unsigned step=0;step<rounds;step++){
 unsigned round=step;
#ifdef NATIVE_FACTORY_GRID
 int grid_reference=step==0;round=grid_reference?0:step-1;
#endif
 unsigned origin=round==1?2048:0;
#ifdef NATIVE_FULL_PREVIEW
 NativeTile tile;if(!ntp_get(round,&tile))goto end;
#endif
#if !defined(NATIVE_FLAT_FIELD) && !defined(NATIVE_CACHE_INPUT)
 (void)target; /* Read validated first frame directly, no six-frame merge job descriptor. */
#else
 (void)target;
#endif
 if(duss_hal_mem_map(input,&mapped)||!mapped)goto end;
 double begin=now();puts("MERGED_NATIVE_WINDOW_FILL_BEGIN");
#ifdef NATIVE_FACTORY_GRID
 if(grid_reference){
  nmr_close(&rows);
  if(!nji_unchanged(&job_sources))goto end;
  unsigned char header[65536];int first=job_sources.fd[0];
  if(pread(first,header,sizeof header,0)!=sizeof header||memcmp(header,"II\052\000",4))goto end;
  uint32_t sub,off,width,height,bits,compression,count,samples;
  if(!ncr_number(header,ncr_u32(header+4),330,&sub)||!ncr_number(header,sub,273,&off)||
     !ncr_number(header,sub,256,&width)||width!=11904||!ncr_number(header,sub,257,&height)||height!=8842||
     !ncr_number(header,sub,258,&bits)||bits!=16||!ncr_number(header,sub,259,&compression)||compression!=1||
     !ncr_number(header,sub,277,&samples)||samples!=1||!ncr_number(header,sub,279,&count)||count!=210510336||
     off<8||(uint64_t)off+count>(uint64_t)job_sources.identity[0].st_size)goto end;
  size_t pos=0;while(pos<count){ssize_t n=pread(first,(unsigned char*)mapped+pos,count-pos,(off_t)off+pos);if(n<0&&errno==EINTR)continue;if(n<=0||cancelled)goto end;pos+=(size_t)n;}
  if(!nji_unchanged(&job_sources))goto end;
  puts("FACTORY_GRID_FIRST_FRAME_UNMODIFIED_SENSOR_INPUT");
 }else{
#endif
#ifdef NATIVE_COLOR_REFERENCE
 if(round==1){
  Frame*first=&rows.job.frame[0];
  if(fseeko(first->f,(off_t)first->offset,SEEK_SET)||fread(mapped,1,210510336,first->f)!=210510336)goto end;
  puts("FIRST_FRAME_NATIVE_SENSOR_REFERENCE_NO_GDC_NO_SCALE_REMAP");
 }else if(!ntw_fill(mapped,210513920u/2,11836,8842,11904,23310,17482,NATIVE_REFERENCE_X,NATIVE_REFERENCE_Y,129,96,row,23310,nmr_read_row,&rows))goto end;
#elif defined(NATIVE_FLAT_FIELD)
 /* Identical full input every round; changing origin affects sampling ONLY.
  * Constant Bayer signal, no scene/illumination changes and no source pixels.
  * A nonuniform output proves spatial processing exists, not which module. */
 for(unsigned y=0;y<8842;y++){
  uint16_t*line=(uint16_t*)mapped+(size_t)y*11904;
  for(unsigned x=0;x<11836;x++)line[x]=12000;
  memset(line+11836,0,(11904-11836)*sizeof *line);
 }
 puts("SYNTHETIC_FLAT_FIELD_CONSTANT_12000_NOT_PHOTO_COLOR_VALIDATION");
#else
 unsigned ox=origin,oy=origin;
#ifdef NATIVE_EDGE_TEST
 ox=origin?11654:8192;oy=origin?8740:6144;
#endif
#ifdef NATIVE_FULL_PREVIEW
 ox=tile.x;oy=tile.y;
#endif
 if(!ntw_fill(mapped,210513920u/2,11836,8842,11904,23310,17482,ox,oy,129,96,row,23310,
#ifdef NATIVE_CACHE_INPUT
 ncr_read_row,&cache
#else
 nmr_read_row,&rows
#endif
 ))goto end;
#endif
#ifdef NATIVE_FACTORY_GRID
 }
#endif
 memset((unsigned char*)mapped+210510336,0,3584);
 printf("OVERLAP_RAW_GLOBAL_SAMPLE_HASH %016llx ORIGIN %u\n",(unsigned long long)overlap_raw_hash(mapped,origin),origin);
 printf("MERGED_NATIVE_WINDOW_FILL_SECONDS %.3f\n",now()-begin);
 if(duss_hal_mem_sync(input,2)||duss_hal_mem_unmap(input))goto end;mapped=NULL;
 nmr_close(&rows);
 if(cancelled||!tile_pool_ok())goto end;
 if(!output&&(duss_hal_mem_alloc(ion,&output,206503936,4096,2,0)||!output))goto end;
 if(duss_hal_mem_get_size(output,&size)||size<206503936||duss_hal_mem_share(input,&ih)||!ih||duss_hal_mem_share(output,&oh)||!oh)goto end;
#ifdef NATIVE_REQUEST_ID_TEST
 unsigned char marked[32768];size_t marked_length=length;const unsigned char*request_metadata=metadata;
 /* Middle request intentionally unmarked: coordinate hook must bypass it. */
 if(round!=1
#ifdef NATIVE_COLOR_REFERENCE
 ||1
#endif
#ifdef NATIVE_EDGE_TEST
 ||1
#endif
#ifdef NATIVE_FULL_PREVIEW
 ||1
#endif
 ){NativeRequestToken token={nonce,round+1,origin,origin};
#ifdef NATIVE_COLOR_REFERENCE
  token.x=NATIVE_REFERENCE_X;token.y=NATIVE_REFERENCE_Y;if(round==1)token.serial=UINT32_C(0x80000002);
#endif
#ifdef NATIVE_EDGE_TEST
  token.x=origin?11654:8192;token.y=origin?8740:6144;
#endif
#ifdef NATIVE_FULL_PREVIEW
  token.x=tile.x;token.y=tile.y;
#ifdef NATIVE_FACTORY_GRID
  if(grid_reference){token.serial=NFGB_REFERENCE_SERIAL;token.x=token.y=0;}
#endif
#endif
  marked_length=nrt_append(marked,sizeof marked,metadata,length,&token);if(!marked_length)goto end;
  request_metadata=marked;
 }
 request=clone_native_metadata(event,ih,oh,request_metadata,(int)marked_length);
#else
 request=clone_native_metadata(event,ih,oh,metadata,(int)length);
#endif
 if(!request||cancelled)goto end;
 if(!c){c=dbus_bus_get_private(1,NULL);if(!c)goto end;dbus_connection_set_exit_on_disconnect(c,0);}
#if defined(NATIVE_COLOR_REFERENCE) || defined(NATIVE_FULL_PREVIEW)
 if(!stop_view_before_render(c)){puts("EXCLUSIVE_VIEW_OFF_NOT_CONFIRMED_NO_RENDER");goto end;}
#endif
 puts("MERGED_NATIVE_WINDOW_SUBMIT_ONCE");begin=now();
#ifdef NATIVE_JOB_RUNTIME
 NativeRequestToken submitted={nonce,round+1,tile.x,tile.y};
#ifdef NATIVE_FACTORY_GRID
 if(grid_reference){submitted.serial=NFGB_REFERENCE_SERIAL;submitted.x=submitted.y=0;}
#endif
#ifdef NATIVE_FACTORY_GRID
 int inputs_unchanged=nji_unchanged(&job_sources),live_state=nvc_get();
 if(!inputs_unchanged||live_state!=0||!nrj_begin(&render,&submitted,njl_clock())){
  printf("FACTORY_GRID_SUBMIT_REJECTED serial=%u inputs=%d view=%d completed=%u inflight=%u reference=%u cancelled=%u failed=%u now=%llu deadline=%llu\n",submitted.serial,inputs_unchanged,live_state,render.completed,render.inflight,render.reference_complete,render.cancelled,render.failed,(unsigned long long)njl_clock(),(unsigned long long)render.deadline);goto end;
 }
#else
 if(!nji_unchanged(&job_sources)||nvc_get()!=0||!nrj_begin(&render,&submitted,njl_clock()))goto end;
#endif
#endif
 /* Opaque, over-sized/aligned DBusError storage; read only the public pointer
  * prefix, let libdbus initialize/free its own ABI fields. */
 extern void dbus_error_init(void*);extern void dbus_error_free(void*);
 union {max_align_t align;unsigned char bytes[64];} bus_error;
 dbus_error_init(&bus_error);
 reply=dbus_connection_send_with_reply_and_block(c,request,20000,&bus_error);
 if(!reply){const char*ename=NULL,*etext=NULL;memcpy(&ename,bus_error.bytes,sizeof ename);memcpy(&etext,bus_error.bytes+sizeof ename,sizeof etext);
  printf("NATIVE_RENDER_BUS_ERROR %s MESSAGE %s\n",ename?ename:"none",etext?etext:"none");}
 dbus_error_free(&bus_error);
 if(!reply||dbus_message_get_type(reply)!=2||strcmp(dbus_message_get_signature(reply),"")){
#ifdef NATIVE_FACTORY_GRID
  printf("FACTORY_GRID_REPLY_REJECTED serial=%u type=%d signature=%s\n",submitted.serial,reply?dbus_message_get_type(reply):-1,reply?dbus_message_get_signature(reply):"none");
#endif
#ifdef NATIVE_JOB_RUNTIME
  unknown=1;
#endif
  goto end;
 }
#ifdef NATIVE_JOB_RUNTIME
 if(!nrj_reply(&render,nonce,submitted.serial,1))goto end;
 /* Hook acknowledgement prevents silently publishing an uncorrected tile
  * if the scoped profile/LSC hook was bypassed or the lease expired. */
#ifdef NATIVE_FACTORY_GRID
 if(!grid_reference)
#endif
 {uint64_t ack[3]={0};int fd=open(NJL_ROOT"/job.ack",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  int ok=fd>=0&&read(fd,ack,sizeof ack)==sizeof ack&&ack[0]==nonce&&ack[1]==round+1&&ack[2]==1;
  if(fd>=0)close(fd);if(!ok)goto end;}
#endif
 printf("MERGED_NATIVE_WINDOW_COMPLETED_SECONDS %.3f\n",now()-begin);
 if(cancelled)goto end;
#ifdef NATIVE_FACTORY_GRID
 if(grid_reference){
  if(!nji_unchanged(&job_sources)||!factory_grid_load(&factory_mesh,nonce))goto end;
  puts("FACTORY_FIRST_FRAME_GRID_BOUND_COMPLETE");
 }else{
#endif
#ifdef NATIVE_FULL_PREVIEW
 void*ymap=NULL;if(duss_hal_mem_map(output,&ymap)||!ymap)goto end;
 double reduce_begin=now();
 int assembled=!duss_hal_mem_sync(output,1)&&!cancelled&&nia_add(&assembly,nonce,&tile,
  ymap,206503936,11776,103251968,preview_cached_sink,&preview,preview_cancel);
 if(duss_hal_mem_unmap(output)||!assembled||cancelled)goto end;
 printf("NATIVE_FULL_PREVIEW_TILE %u OF 9 COMPLETE\n",round+1);
 printf("NATIVE_PREVIEW_REDUCE_SECONDS %.3f\n",now()-reduce_begin);
#elif defined(NATIVE_TILE_OVERLAP)
 void*ymap=NULL;if(duss_hal_mem_map(output,&ymap)||!ymap)goto end;
 if(duss_hal_mem_sync(output,1)){duss_hal_mem_unmap(output);goto end;}
 overlap_samples(ymap,origin,samples[round]);if(round<2)shift_grid(ymap,origin,patches,centers);if(duss_hal_mem_unmap(output))goto end;
 snprintf(path,sizeof path,"%s/samples%u.bin",root,round);
 int samplefd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(samplefd<0)goto end;
 ssize_t saved=write(samplefd,samples[round],sizeof samples[round]);int closed=close(samplefd);
 if(saved!=sizeof samples[round]||closed)goto end;
 printf("OWN_SAMPLES_EXPORTED ROUND %u BYTES %zu\n",round,sizeof samples[round]);
#else
 rc=native_jpeg_trial(ion,output);
 if(!rc)puts("ACTUAL_MERGED_WINDOW_NATIVE_RENDER_JPEG_PASS NOT_FULL_407MP_COLOR_VALIDATION");
#endif
#ifdef NATIVE_FACTORY_GRID
 }
#endif
 dbus_message_unref(reply);reply=NULL;dbus_message_unref(request);request=NULL;
 }
#ifdef NATIVE_FACTORY_REFERENCE_ONLY
 rc=nfm_complete(&factory_mesh,nonce)&&nrj_can_release(&render)&&render.reference_complete&&!cancelled?0:1;
#ifdef NATIVE_FACTORY_REFERENCE_PREVIEW
 if(rc||!nji_unchanged(&job_sources))goto end;
 /* Same first-frame native output, not the merged raster or a fitted warp.
  * Encoding occurs only after known completion; no album publication. */
 rc=1;
 unsigned char*reference_packed=malloc(1920u*1440*2);void*reference_map=NULL;
 if(!reference_packed)goto end;
 if(duss_hal_mem_map(output,&reference_map)||!reference_map){free(reference_packed);goto end;}
 int reference_ok=!duss_hal_mem_sync(output,1)&&nrr_reduce(reference_map,206503936,11656,8742,11776,103251968,
  reference_packed,1920u*1440*2,1920,1440,preview_cancel,NULL);
 if(duss_hal_mem_unmap(output))reference_ok=0;
 if(!reference_ok||cancelled){free(reference_packed);goto end;}
 if(duss_hal_mem_free(output)){free(reference_packed);goto end;}output=NULL;
 if(duss_hal_mem_free(input)){free(reference_packed);goto end;}input=NULL;
 if(duss_hal_mem_alloc(ion,&preview_mem,2048u*1440*2,4096,2,0)||!preview_mem){free(reference_packed);goto end;}
 if(duss_hal_mem_map(preview_mem,&mapped)||!mapped){free(reference_packed);goto end;}
 for(unsigned y=0;y<2880;y++){
  memcpy((unsigned char*)mapped+(size_t)y*2048,reference_packed+(size_t)y*1920,1920);
  memset((unsigned char*)mapped+(size_t)y*2048+1920,0,128);
 }
 free(reference_packed);
 int reference_synced=duss_hal_mem_sync(preview_mem,2),reference_unmapped=duss_hal_mem_unmap(preview_mem);mapped=NULL;
 if(reference_synced||reference_unmapped||cancelled||!nji_unchanged(&job_sources))goto end;
 rc=native_jpeg_trial_sized(ion,preview_mem,1920,1440,2048,2048u*1440,2048u*1440*2);
 if(!rc)puts("FIRST_FRAME_ORIGINAL_GDC_PREVIEW_CONTROL_NO_MERGE_NO_WARP_NO_PUBLICATION");
#endif
 puts("FACTORY_REFERENCE_ONLY_NO_MERGE_NO_PREVIEW_PUBLICATION");goto end;
#endif
#ifdef NATIVE_FULL_PREVIEW
 if(!nia_complete(&assembly)||cancelled)goto end;
#ifdef NATIVE_JOB_RUNTIME
 if(!nrj_can_publish(&render)||!nji_unchanged(&job_sources))goto end;
#endif
 /* Release full-window ION before allocating the small preview/encoder. */
 if(duss_hal_mem_free(output))goto end;output=NULL;
 if(duss_hal_mem_free(input))goto end;input=NULL;
 if(duss_hal_mem_alloc(ion,&preview_mem,2048u*1440*2,4096,2,0)||!preview_mem)goto end;
 if(duss_hal_mem_map(preview_mem,&mapped)||!mapped)goto end;
 unsigned char*packed=malloc(1920u*1440*2);if(!packed)goto end;
 int complete=npa_finish(&preview,&assembly,packed,1920u*1440*2);
#ifdef NATIVE_FACTORY_GRID
 if(complete){
  unsigned char*warped=malloc(1920u*1440*2);
  complete=warped&&npd_apply(&factory_mesh,nonce,packed,1920u*1440*2,1920,1440,warped,1920u*1440*2,NATIVE_FACTORY_CHROMA_SITING,preview_cancel,NULL);
  free(packed);packed=warped;
  if(complete)puts("FACTORY_GRID_PREVIEW_WARP_COMPLETE_CHROMA_FILTER_VALIDATION_PENDING");
 }
#endif
 if(complete)for(unsigned y=0;y<2880;y++){
  memcpy((unsigned char*)mapped+(size_t)y*2048,packed+(size_t)y*1920,1920);
  memset((unsigned char*)mapped+(size_t)y*2048+1920,0,128);
 }
 free(packed);
 int synced=duss_hal_mem_sync(preview_mem,2),unmapped=duss_hal_mem_unmap(preview_mem);mapped=NULL;
 if(!complete||synced||unmapped||cancelled)goto end;
 rc=native_jpeg_trial_sized(ion,preview_mem,1920,1440,2048,2048u*1440,2048u*1440*2);
 if(!rc)puts("NATIVE_FULL_407MP_COVERAGE_TO_PREVIEW_JPEG_PASS_NOT_PUBLISHED");
#endif
#ifdef NATIVE_TILE_OVERLAP
 sample_diff(samples[0],samples[1]);
 puts("SAME_ORIGIN_REPEAT_CONTROL");sample_diff(samples[0],samples[2]);
#if !defined(NATIVE_EDGE_TEST) && !defined(NATIVE_COLOR_REFERENCE)
 report_shift(patches,centers);
#elif defined(NATIVE_EDGE_TEST)
 puts("EDGE_ORIGINS_8192_6144_AND_11654_8740_NEGATIVE_CENTER_ENCODING_TEST");
#else
 puts("FIRST_FRAME_NATIVE_SCALE_COMPARISON_GEOMETRY_NO_GDC_CONTROL");
#endif
 puts("OVERLAPPING_NATIVE_WINDOWS_COMPARED_NO_COLOR_EQUIVALENCE_ASSUMED");rc=0;
#endif
end:
#ifdef NATIVE_JOB_RUNTIME
 if(!nrj_can_release(&render)){
  unknown=1;int marker=open(NJL_ROOT"/recovery.required",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(marker>=0){dprintf(marker,"nonce=%016llx pid=%u\n",(unsigned long long)nonce,service.pid);fsync(marker);close(marker);}
  puts("NATIVE_OUTCOME_UNKNOWN_RETAINING_BUFFERS_UNTIL_SERVICE_QUIESCENT");
  if(!nsq_stop(&service))while(!nsq_gone(&service)){struct timespec delay={0,100000000};nanosleep(&delay,NULL);}
  puts("NATIVE_SERVICE_QUIESCENT_BUFFERS_RELEASE_ALLOWED");rc=70;
 }
#endif
#ifdef NATIVE_FULL_PREVIEW
 if(mapped&&preview_mem){if(duss_hal_mem_unmap(preview_mem))rc=3;mapped=NULL;}
 if(preview_mem&&duss_hal_mem_free(preview_mem))rc=3;
 npa_free(&preview);
#ifdef NATIVE_FACTORY_GRID
 nfm_free(&factory_mesh);
#endif
#endif
#ifdef NATIVE_CACHE_INPUT
 ncr_close(&cache);
#endif
 if(mapped&&duss_hal_mem_unmap(input))rc=3;
 if(reply)dbus_message_unref(reply);if(request)dbus_message_unref(request);if(event)dbus_message_unref(event);
 if(c){dbus_connection_close(c);dbus_connection_unref(c);}
 if(output&&duss_hal_mem_free(output))rc=3;if(input&&duss_hal_mem_free(input))rc=3;
 if(started&&duss_hal_device_stop(ion,NULL))rc=3;if(ion&&duss_hal_device_close(ion))rc=3;if(initialized&&duss_hal_deinitialize())rc=3;
#ifdef NATIVE_JOB_RUNTIME
 if(lease_owned&&!njl_remove(&lease))rc=70;
 if(view_owned){
  if(unknown){dprintf(view_fd,"SERVICE_RECOVERY_REQUIRED\n");rc=70;}
  else if(!nvc_set(view.original)||nvc_get()!=view.original){dprintf(view_fd,"VIEW_RESTORE_FAILED\n");rc=70;}
  else dprintf(view_fd,"VIEW_RESTORED %d\n",view.original);
  if(fsync(view_fd))rc=70;
 }
 if(view_fd>=0)close(view_fd);
 nji_close(&job_sources);
#endif
 free(patches);free(centers);free(row);nmr_close(&rows);printf("OBSERVER_EXIT %d NO_CAPTURE_NO_PHOTO_WRITES\n",rc);return rc;
}
