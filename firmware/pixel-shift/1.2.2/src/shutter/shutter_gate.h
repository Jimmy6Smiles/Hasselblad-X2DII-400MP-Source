/* 自写门控会话策略。失效后只允许显式 OFF 解锁，不由心跳自动重新武装。 */
#ifndef PS_SHUTTER_GATE_H
#define PS_SHUTTER_GATE_H
#include "shutter_policy.h"
typedef struct {uint64_t magic,session,sequence,deadline_ms;uint32_t mode,ready;} Gate;
_Static_assert(sizeof(Gate)==40,"gate ABI");
static void ps_gate_fail(Gate *g,int *failed){*failed=1;g->mode=PS_GATE_FAILED;g->ready=0;}
static void ps_gate_update(Gate *g,PsShutterPolicy *p,int *failed,const Gate *next,int valid,uint64_t time){
 if(!valid){if(g->mode!=PS_GATE_OFF)ps_gate_fail(g,failed);return;}
 if(next->mode==PS_GATE_OFF){
  /* 旧 down 的 release 仍需被吞掉；只允许新会话重用请求序号。 */
  if(g->session!=next->session)p->consumed=0;
  *g=*next;g->ready=0;*failed=0;return;
 }
 if(*failed||!next->session||!time||next->deadline_ms<time||next->deadline_ms-time>2000||
    (g->session&&g->session!=next->session)||next->sequence<p->consumed){ps_gate_fail(g,failed);return;}
 *g=*next;
}
#endif
