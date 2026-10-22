/* Copy only the eight calibration grids of an authenticated first-frame
 * native render. No pixels, filenames, HAL calls, or global state in this file.
 * guard MUST check live state-machine nonce/serial + lease/source binding.
 * Return -1 for unmap failure (caller must retain recovery diagnostics). */
#ifndef NATIVE_FACTORY_GRID_COLLECT_H
#define NATIVE_FACTORY_GRID_COLLECT_H
#include "native_factory_grid_bundle.h"
typedef struct {
 int(*size)(void*,uint32_t*);
 int(*map)(void*,void**);
 int(*unmap)(void*);
 int(*guard)(void*,uint64_t,uint32_t);
 void*context;
} NativeFactoryGridOps;
static inline int nfgc_collect(unsigned char*out,size_t capacity,size_t*written,uint64_t nonce,
 const unsigned char*proc,size_t proc_bytes,const NativeFactoryGridOps*ops){
 if(written)*written=0;
 if(!out||!written||!nonce||!proc||proc_bytes<0xc8||!ops||!ops->size||!ops->map||!ops->unmap||!ops->guard||
    !ops->guard(ops->context,nonce,NFGB_REFERENCE_SERIAL)||proc[0]!=1)return 0;
 const unsigned char*image=NULL;memcpy(&image,proc+0x88,sizeof image);
 /* image is the factory's already-consumed framebuffer, not user input. */
 if(!image||nfm_u32(image+0x38)!=11656||nfm_u32(image+0x3c)!=8742)return 0;
 unsigned char*copy=malloc(NFGB_LIMIT),*packed=malloc(NFGB_LIMIT);int rc=0;
 const unsigned char*grids[8];size_t sizes[8],used=0;
 if(!copy||!packed)goto end;
 for(unsigned i=0;i<8;i++){
  if(!ops->guard(ops->context,nonce,NFGB_REFERENCE_SERIAL))goto end;
  void*handle=NULL,*mapped=NULL;uint32_t bytes=0,offset=nfm_u32(proc+16+16*i);
  memcpy(&handle,proc+8+16*i,sizeof handle);
  if(!handle||ops->size(handle,&bytes)||bytes>8u*1024u*1024u||offset>bytes||bytes-offset<128)goto end;
  if(ops->map(handle,&mapped))goto end;
  int valid=0;
  if(mapped){
   const unsigned char*p=(const unsigned char*)mapped+offset;
   size_t size=nfm_u32(p+1);
   if(p[0]==2&&size<=bytes-offset-128&&size<=32768-128&&size+128<=NFGB_LIMIT-used){
    sizes[i]=size+128;grids[i]=copy+used;memcpy(copy+used,p,sizes[i]);used+=sizes[i];valid=1;
   }
  }
  if(ops->unmap(handle)){rc=-1;goto end;}
  if(!valid)goto end;
 }
 size_t n=nfgb_pack(packed,NFGB_LIMIT,nonce,grids,sizes);
 if(!n||n>capacity||!ops->guard(ops->context,nonce,NFGB_REFERENCE_SERIAL))goto end;
 memcpy(out,packed,n);*written=n;rc=1;
end:free(copy);free(packed);return rc;
}
#endif
