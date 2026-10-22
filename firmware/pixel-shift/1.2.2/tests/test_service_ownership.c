#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
static int result, error;
static int probe(const char *path, struct stat *st) {
 (void)st;
 assert(!strcmp(path,"/system/camera-extension-host-v1"));
 errno=error;return result;
}
#define lstat probe
#include "../startup/unified-v2/service_ownership.h"
int main(void) {
 result=-1;error=ENOENT;assert(!shared_service_owner());
 result=0;error=ENOENT;assert(shared_service_owner());
 result=-1;error=EACCES;assert(shared_service_owner());
 error=EIO;assert(shared_service_owner());
 error=ELOOP;assert(shared_service_owner());
 puts("COMMON_SERVICE_OWNER_PRESENT_UNKNOWN_DENIED_ABSENT_LEGACY_PASS");
 return 0;
}
