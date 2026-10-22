#define PS_JOB_TEST
#define PS_HOST_TEST_NONATOMIC_RENAME
#define PS_ODIN "/nonexistent-ps-host-odin"
#define PS_DATA "data"
#define PS_BIN "bin"
#define PS_MEDIA "media"
#include "../src/coordinator/integrated_job.c"
#include <assert.h>
int main(void){
 char tmp[]="/tmp/ps-preflight-XXXXXX";assert(mkdtemp(tmp)&&!chdir(tmp));
 /* Real coordinator entry: property-read failure occurs before any settings or
  * capture child, and must request menu restoration (20), not latch failure. */
 assert(job("123","2",NULL)==20);
 assert(access("data",F_OK)&&access("media",F_OK));
 puts("PASS production coordinator pre-capture failure returns restore-menu code");
}
