/* Pixel Shift backend entry owned by the immutable feature package.
 * The common launcher is its only supported parent. */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>

#ifndef PS_FEATURE_ROOT
#define PS_FEATURE_ROOT "/system/camera-mods/pixel-shift-400mp/1.2.4-rc4"
#endif

static volatile sig_atomic_t stopping;
static void stop_requested(int sig) { (void)sig; stopping = 1; }

/* Own only these two children. Never signal camera services or another MOD. */
static int services(void) {
    pid_t children[2] = {0, 0};
    struct sigaction action = {0};
    action.sa_handler = stop_requested;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, 0);
    sigaction(SIGINT, &action, 0);
    for (int i = 0; i < 2 && !stopping; ++i) {
        children[i] = fork();
        if (children[i] < 0) { children[i] = 0; stopping = 1; break; }
        if (!children[i]) {
            if (i == 0) {
                int fd = open("/dev/x2d2-integrated-v1/zoom.log", O_WRONLY|O_CREAT|O_APPEND, 0600);
                if (fd < 0) _exit(126);
                dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd);
                execl(PS_FEATURE_ROOT "/tools/zoom-server", "zoom-server",
                      "/mnt/media_rw/ssd/DCIM", "/mnt/media_rw/cfe/DCIM",
                      "/dev/x2d2-integrated-v1/scratch", "38407", "resident", (char *)0);
            } else {
                execl(PS_FEATURE_ROOT "/integration-service", "integration-service",
                      "--resident-retain-inputs", (char *)0);
            }
            _exit(127);
        }
    }
    while (!stopping) {
        for (int i = 0; i < 2; ++i) {
            int status;
            pid_t got = waitpid(children[i], &status, WNOHANG);
            if (got == children[i] || (got < 0 && errno == ECHILD)) {
                children[i] = 0; stopping = 1; break;
            }
        }
        struct timespec pause = {0, 100000000};
        nanosleep(&pause, 0);
    }
    for (int i = 0; i < 2; ++i) if (children[i] > 0) kill(children[i], SIGTERM);
    /* Do not force-kill a child potentially finishing a capture/DMA request. */
    for (int i = 0; i < 2; ++i) if (children[i] > 0)
        while (waitpid(children[i], 0, 0) < 0 && errno == EINTR) {}
    return 1;
}

int main(int argc, char **argv) {
    const char *shared = getenv("CM_SHARED_BACKEND");
    if (geteuid() || !shared || strcmp(shared, "1") || getppid() <= 1) return 2;
    if (argc == 2 && !strcmp(argv[1], "--services")) return services();
    if (argc != 1) return 2;
    setenv("PS_FEATURE_ROOT", PS_FEATURE_ROOT, 1);
    execl("/system/bin/sh", "sh", PS_FEATURE_ROOT "/backend-start.sh", (char *)0);
    return 127;
}
