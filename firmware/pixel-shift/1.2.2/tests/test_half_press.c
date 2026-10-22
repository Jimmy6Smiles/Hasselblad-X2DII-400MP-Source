#include "../src/shutter/shutter_policy.h"
#include <assert.h>
#include <stdio.h>
#define E(key,down,mode,seq) ps_key_event(&p,key,down,mode,1,1,seq)
int main(void){
 PsShutterPolicy p={0};
 assert(E(0x41,1,PS_GATE_OFF,1)==PS_KEY_PASS);
 assert(E(0x41,0,PS_GATE_OFF,1)==PS_KEY_PASS);
 assert(E(0x41,1,PS_GATE_ARMED,2)==PS_KEY_PASS);
 assert(E(0x45,1,PS_GATE_ARMED,2)==PS_KEY_QUEUE);
 assert(E(0x41,0,PS_GATE_BUSY,2)==PS_KEY_PASS);
 assert(E(0x41,1,PS_GATE_BUSY,2)==PS_KEY_SWALLOW);
 assert(E(0x41,1,PS_GATE_BUSY,2)==PS_KEY_SWALLOW);
 assert(E(0x41,0,PS_GATE_OFF,3)==PS_KEY_SWALLOW);
 assert(E(0x41,1,PS_GATE_OFF,3)==PS_KEY_PASS);
 assert(E(0x41,0,PS_GATE_OFF,3)==PS_KEY_PASS);
 assert(E(0x45,0,PS_GATE_OFF,3)==PS_KEY_SWALLOW);
 assert(E(0x45,1,PS_GATE_ARMED,4)==PS_KEY_QUEUE);
 assert(E(0x41,1,PS_GATE_ARMED,4)==PS_KEY_SWALLOW);
 assert(E(0x41,0,PS_GATE_ARMED,4)==PS_KEY_SWALLOW);
 assert(E(0x41,1,PS_GATE_FAILED,4)==PS_KEY_SWALLOW);
 assert(E(0x41,0,PS_GATE_FAILED,4)==PS_KEY_SWALLOW);
 assert(E(0x41,1,PS_GATE_ARMED,5)==PS_KEY_PASS);
 assert(E(0x41,0,PS_GATE_ARMED,5)==PS_KEY_PASS);
 assert(E(0x99,1,PS_GATE_BUSY,5)==PS_KEY_PASS);
 puts("HALF_PRESS_PAIRING_PASS");
}
