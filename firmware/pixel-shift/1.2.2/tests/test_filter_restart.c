#include <assert.h>
#include "../startup/unified-v2/filter_restart.h"
int main(void){
 PsFilterRestart s={0};
 assert(ps_filter_restart_tick(&s,0,0,1,1));
 assert(!ps_filter_restart_tick(&s,1,0,1,0));
 assert(ps_filter_restart_tick(&s,10,1,1,1));
 assert(ps_filter_restart_tick(&s,110,1,1,0));
 assert(ps_filter_restart_tick(&s,60009,1,1,0));
 assert(!ps_filter_restart_tick(&s,60010,1,1,0));
 assert(ps_filter_restart_tick(&s,60011,1,1,1));
 s=(PsFilterRestart){0};
 assert(ps_filter_restart_tick(&s,10,1,0,1));
 assert(!ps_filter_restart_tick(&s,11,1,0,0));
 assert(ps_filter_restart_tick(&s,20,1,1,0));
 assert(!ps_filter_restart_tick(&s,30,0,1,0));
 assert(ps_filter_restart_tick(&s,40,0,1,1));
 assert(ps_filter_restart_tick(&s,50,1,1,0));
 assert(ps_filter_restart_tick(&s,60,0,1,1));
 assert(!ps_filter_restart_tick(&s,-1,0,1,1));
 assert(ps_filter_restart_tick(&s,100,1,1,0));
 assert(!ps_filter_restart_tick(&s,99,1,1,0));
 return 0;
}
