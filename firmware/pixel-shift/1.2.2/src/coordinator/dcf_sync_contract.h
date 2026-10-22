#ifndef PS_DCF_SYNC_H
#define PS_DCF_SYNC_H
#include "album_refresh_contract.h"
typedef struct {
 uint64_t magic,token,deadline_ms,device,inode,bytes;
 uint32_t expected,target;
 char folder[14];uint16_t reserved;
} PsDcfSync;
#define PS_DCF_MAGIC UINT64_C(0x3146434453595350)
_Static_assert(sizeof(PsDcfSync)==72,"DCF wire ABI");
static inline int ps_dcf_valid(const PsDcfSync *r,const char *folder,uint64_t now){
 return r->magic==PS_DCF_MAGIC&&r->token&&r->device&&r->inode&&r->bytes>(r->reserved==1?10000:815010840)&&
  r->reserved<=1&&ps_album_folder(r->folder)&&!memcmp(r->folder,folder,14)&&now&&
  r->deadline_ms>=now&&r->deadline_ms-now<=60000&&r->expected<9999999&&
  r->target>=r->expected&&r->target<9999999&&r->target-r->expected<=10000;
}
static inline int ps_dcf_indices(const PsDcfSync *r,int global,int next,int persistent){
 /* Never rewind any allocator, and never accept a concurrent capture. */
 return global==(int)r->expected&&persistent==global&&next>0&&
  next<=((int)r->target+1)&&next>=global;
}
#endif
