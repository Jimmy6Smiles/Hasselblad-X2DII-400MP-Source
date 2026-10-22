/* 自写按键门控。只返回动作，不依赖厂商结构、线程或文件。 */
#ifndef PS_SHUTTER_POLICY_H
#define PS_SHUTTER_POLICY_H
#include <stdint.h>
enum { PS_KEY_PASS,PS_KEY_SWALLOW,PS_KEY_QUEUE };
enum { PS_GATE_OFF,PS_GATE_ARMED,PS_GATE_BUSY,PS_GATE_DIAGNOSTIC,PS_GATE_FAILED };
typedef struct {int swallowed_down;uint64_t consumed;int half_swallowed,half_forwarded;} PsShutterPolicy;
static int ps_key_event(PsShutterPolicy *p,unsigned key,int down,int mode,
                       int fresh,int ready,uint64_t sequence){
 /* Firmware 1.3.16.2 InputControl::pump: 0x41 -> halfPress(bool).
  * Match releases to presses: a half press already passed before capture must
  * still get its release, while a suppressed press must never leak a release.
  * consumed closes the gap between queuing full press and backend BUSY. */
 if(key==0x41){
  if(!down){
   if(p->half_swallowed){p->half_swallowed=0;return PS_KEY_SWALLOW;}
   p->half_forwarded=0;return PS_KEY_PASS;
  }
  if(p->half_swallowed)return PS_KEY_SWALLOW;
  int blocked=mode==PS_GATE_BUSY||mode==PS_GATE_FAILED||
     (mode==PS_GATE_ARMED&&sequence&&sequence==p->consumed);
  if(p->half_forwarded)return blocked?PS_KEY_SWALLOW:PS_KEY_PASS;
  if(blocked){
   p->half_swallowed=1;return PS_KEY_SWALLOW;
  }
  p->half_forwarded=1;return PS_KEY_PASS;
 }
 if(key!=0x45)return PS_KEY_PASS;
 if(!down&&p->swallowed_down){p->swallowed_down=0;return PS_KEY_SWALLOW;}
 if(mode==PS_GATE_OFF)return PS_KEY_PASS;
 if(!down)return PS_KEY_SWALLOW;
 int repeat=p->swallowed_down;p->swallowed_down=1;
 if(mode==PS_GATE_ARMED&&!repeat&&fresh&&ready&&sequence&&sequence!=p->consumed){
  p->consumed=sequence;return PS_KEY_QUEUE;
 }
 /* 已选择扩展但后台失败时，不能意外落回普通单拍。 */
 return PS_KEY_SWALLOW;
}
#endif
