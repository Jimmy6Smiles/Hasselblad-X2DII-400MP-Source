/* Fail closed: macOS host fixtures cannot provide Linux parent-death signals. */
#ifndef __APPLE__
#error host-only include path
#endif
#include <errno.h>
#define PR_SET_PDEATHSIG 1
static int prctl(int option,int signal){(void)option;(void)signal;errno=ENOTSUP;return -1;}
