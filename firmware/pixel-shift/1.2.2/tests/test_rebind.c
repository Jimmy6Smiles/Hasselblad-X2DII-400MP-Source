#include "fast_capture_guard.h"
#include <assert.h>
#include <stdio.h>
int main(void){PsFastGuard p;ps_fast_init(&p);
 assert(!ps_fast_rebind_off(&p,0,1,2,3));
 uint32_t packet[]={0x301,1};assert(ps_fast_sent(&p,packet,8));
 assert(!ps_fast_rebind_off(&p,0,1,2,3));ps_fast_ack(&p);
 assert(!ps_fast_rebind_off(&p,1,1,2,3));assert(!ps_fast_rebind_off(&p,0,0,2,3));
 assert(ps_fast_rebind_off(&p,0,1,2,3));assert(ps_fast_blocked(&p,1,2,3));
 assert(!ps_fast_blocked(&p,1,2,4));assert(ps_fast_rebind_off(&p,0,1,2,4));
 assert(ps_fast_blocked(&p,1,2,4));p.fault=1;assert(!ps_fast_rebind_off(&p,0,1,2,5));
 puts("REBIND_REAL_ACK_TEST_PASS");}
