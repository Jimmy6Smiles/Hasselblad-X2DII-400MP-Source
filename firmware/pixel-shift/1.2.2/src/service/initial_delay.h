/* 自写倒计时契约：界面与采集共享 CLOCK_MONOTONIC 截止时间，不各自重启计时。 */
#ifndef PS_INITIAL_DELAY_H
#define PS_INITIAL_DELAY_H
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
static inline int ps_delay_parse(const char *text,int64_t now,int64_t *deadline){
 if(!text||!*text||now<0)return 0;
 for(const char *p=text;*p;p++)if(*p<'0'||*p>'9'||p-text>=18)return 0;
 char *end;errno=0;long long v=strtoll(text,&end,10);
 if(errno||*end||v<=0)return 0;
 /* 有界容忍预检耗时；陈旧任务不能复用此参数启动。 */
 if((v>now&&v-now>60000)||(v<=now&&now-v>240000))return 0;
 *deadline=(int64_t)v;return 1;
}
static inline int ps_delay_remaining(int64_t deadline,int64_t now){
 if(now<0||deadline<=now||deadline-now>60000)return 0;
 return (int)((deadline-now+999)/1000);
}
#endif
