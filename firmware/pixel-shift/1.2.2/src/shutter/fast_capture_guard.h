/* 自写快速拍摄许可确认策略；无厂商对象、I/O 或设备操作。 */
#ifndef PS_FAST_CAPTURE_GUARD_H
#define PS_FAST_CAPTURE_GUARD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
 unsigned values[32],head,count;
 int acknowledged,fault;
 uint64_t seen_session,seen_sequence;
 unsigned seen_mode;
} PsFastGuard;
static void ps_fast_init(PsFastGuard *p){memset(p,0,sizeof *p);p->acknowledged=-1;}
static int ps_fast_check(PsFastGuard *p,int original,unsigned mode,uint64_t session,uint64_t sequence){
 p->seen_mode=mode;p->seen_session=session;p->seen_sequence=sequence;
 return mode?0:original;
}
/* 仅经验证的调用点使用禁止条件；非目标调用不得污染确认状态。 */
static inline unsigned char ps_fast_inhibit(PsFastGuard *p,unsigned char original,int target,unsigned mode,uint64_t session,uint64_t sequence){
 if(!target)return original;
 (void)ps_fast_check(p,0,mode,session,sequence);
 return mode?1:original;
}
/* 仅原厂 setReady 产生的 8 字节 0x301 消息；值是 !allowed。 */
static int ps_fast_sent(PsFastGuard *p,const void *data,size_t length){
 uint32_t words[2];if(length!=8||!data)return 0;memcpy(words,data,8);
 if(words[0]!=0x301)return 0;
 if(words[1]>1||p->count==32){p->fault=1;return 1;}
 p->values[(p->head+p->count)%32]=!words[1];p->count++;return 1;
}
static void ps_fast_write_result(PsFastGuard *p,long n){if(n!=8)p->fault=1;}
/* 原厂 type=4 完成队列头 setReady。这里只观察，不消费或修改原包。 */
static void ps_fast_ack(PsFastGuard *p){
 if(!p->count){p->fault=1;return;}
 p->acknowledged=(int)p->values[p->head];p->head=(p->head+1)%32;p->count--;
}
static int ps_fast_blocked(const PsFastGuard *p,unsigned mode,uint64_t session,uint64_t sequence){
 return mode&&session&&sequence&&!p->fault&&!p->count&&p->acknowledged==0&&
  p->seen_mode==mode&&p->seen_session==session&&p->seen_sequence==sequence;
}
/* Owner-thread native refresh may short-circuit its getter when already off.
 * Rebind only an actual off cache AND a completed real off acknowledgement.
 * This never manufactures an acknowledgement or clears a fault. */
static int ps_fast_rebind_off(PsFastGuard *p,unsigned cache,unsigned mode,uint64_t session,uint64_t sequence){
 if(cache||mode!=1||!session||!sequence||p->fault||p->count||p->acknowledged!=0)return 0;
 p->seen_mode=mode;p->seen_session=session;p->seen_sequence=sequence;return 1;
}
#endif
