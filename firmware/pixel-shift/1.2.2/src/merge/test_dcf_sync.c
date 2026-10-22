#include <assert.h>
#include <stdio.h>
#include "dcf_sync_contract.h"
int main(void){
 PsDcfSync r={.magic=PS_DCF_MAGIC,.token=123,.deadline_ms=60000,.device=1,.inode=2,.bytes=816320512,.expected=2719,.target=2720,.folder="/cfe/999HASBL"};
 assert(ps_dcf_valid(&r,r.folder,100));assert(ps_dcf_indices(&r,2719,2720,2719));
 assert(!ps_dcf_indices(&r,2720,2720,2720));assert(!ps_dcf_indices(&r,2719,2722,2719));
 assert(!ps_dcf_indices(&r,2719,2718,2719));assert(!ps_dcf_indices(&r,2719,2720,2718));
 assert(!ps_dcf_valid(&r,"/ssd/999HASBL",100));assert(!ps_dcf_valid(&r,r.folder,60001));
 r.target=2718;assert(!ps_dcf_valid(&r,r.folder,100));r.target=9999999;assert(!ps_dcf_valid(&r,r.folder,100));
 r.target=12720;assert(!ps_dcf_valid(&r,r.folder,100));r.target=2720;
 for(int i=0;i<10000;i++){r.expected=(unsigned)i;r.target=(unsigned)i+1;assert(ps_dcf_valid(&r,r.folder,100));assert(ps_dcf_indices(&r,i,i+1,i));}
 puts("DCF_POLICY_BOUNDARIES_AND_10000_FORWARD_TRANSITIONS_PASS_NOT_DEVICE");return 0;
}
