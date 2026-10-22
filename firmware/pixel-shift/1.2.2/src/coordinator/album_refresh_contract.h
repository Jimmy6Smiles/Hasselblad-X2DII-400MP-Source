/* 自写 RAM 请求契约；刷新应答仅表示请求已受理，不是成片登记确认。 */
#ifndef PS_ALBUM_REFRESH_CONTRACT_H
#define PS_ALBUM_REFRESH_CONTRACT_H
#include <stdint.h>
#include <string.h>
typedef struct {uint64_t magic,token,deadline_ms;char folder[14];uint16_t reserved;} PsAlbumRefresh;
#define PS_ALBUM_REFRESH_MAGIC UINT64_C(0x315246414c425350)
_Static_assert(sizeof(PsAlbumRefresh)==40,"refresh wire layout");
static inline int ps_album_folder(const char *s){
 return s[13]==0&&s[0]=='/'&&s[4]=='/'&&(!memcmp(s+1,"ssd",3)||!memcmp(s+1,"cfe",3))&&
 s[5]>='1'&&s[5]<='9'&&s[6]>='0'&&s[6]<='9'&&s[7]>='0'&&s[7]<='9'&&!memcmp(s+8,"HASBL",5);
}
static inline int ps_album_refresh_valid(const PsAlbumRefresh *r,const char folder[14],uint64_t tick){
 return r->magic==PS_ALBUM_REFRESH_MAGIC&&r->token&&r->reserved==0&&tick&&
 r->deadline_ms>=tick&&r->deadline_ms-tick<=60000&&ps_album_folder(r->folder)&&!memcmp(r->folder,folder,14);
}
/* 只有本线程持有、当前可浏览、非清理中且已缓存的原厂目录才能失效。 */
static inline int ps_album_refresh_allowed(int same_thread,int right_type,unsigned enabled,unsigned removed,unsigned browsed){
 return same_thread&&right_type&&enabled==1&&removed==0&&browsed==1;
}
#endif
