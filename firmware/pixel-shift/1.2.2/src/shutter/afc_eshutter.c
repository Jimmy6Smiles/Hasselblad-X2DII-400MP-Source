#include <stdint.h>
#include <stddef.h>

typedef uintptr_t U;
struct DlInfo { U addr; const char *name; const void *phdr; unsigned short phnum; };
extern int open(const char *, int, ...), close(int), mprotect(void *, U, int), getpid(void);
extern long readlink(const char *, char *, U), write(int, const void *, U);
extern int snprintf(char *, U, const char *, ...);
extern int dl_iterate_phdr(int (*)(struct DlInfo *, U, void *), void *);

enum {
    UPDATE_FOCUS_ESHUTTER_BRANCH = 0x1d0f60,
    CAMERA_VTABLE = 0x8bfa20,
    CAMERA_VPTR = CAMERA_VTABLE + 16,
    ENABLED_MODES_SLOT = 0x2b0,
    ENABLED_MODES_ADDR = 0x1e4658,
    ORIGINAL_ESHUTTER_BRANCH = 0x37000160,
    AARCH64_NOP = 0xd503201f,
    AFC_BIT = 4,
};

static U base;
static unsigned (*original_enabled_modes)(void *);

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int main_base(struct DlInfo *d, U n, void *x) {
    (void)n; (void)x;
    if (!d->name || !d->name[0] || equal(d->name, "/system/bin/camera-service")) {
        base = d->addr;
        return 1;
    }
    return 0;
}

__attribute__((noinline)) static unsigned enabled_modes(void *self) {
    /* updateFocusMode retains the lens and drive-mode checks.  This only keeps
       the AF-C capability bit from being hidden by the e-shutter mask. */
    return original_enabled_modes(self) | AFC_BIT;
}

static void report(int code) {
    char text[192];
    int n = snprintf(text, sizeof(text),
        "{\"protocol\":1,\"ready\":%s,\"pid\":%d,\"reason\":%d}\n",
        code == 0 ? "true" : "false", getpid(), code);
    int fd = open("/tmp/x2d2-afc-electronic.json", 0xA0241, 0644);
    if (fd >= 0) { write(fd, text, (U)n); close(fd); }
}

static int attach(void) {
    U *table = (U *)(base + CAMERA_VPTR);
    uint32_t *branch = (uint32_t *)(base + UPDATE_FOCUS_ESHUTTER_BRANCH);
    if (table[ENABLED_MODES_SLOT / 8] != base + ENABLED_MODES_ADDR) return 2;
    if (__atomic_load_n(branch, __ATOMIC_ACQUIRE) != ORIGINAL_ESHUTTER_BRANCH) return 3;

    original_enabled_modes = (void *)table[ENABLED_MODES_SLOT / 8];
    U code_page = (U)branch & ~(U)4095;
    U table_page = (U)&table[ENABLED_MODES_SLOT / 8] & ~(U)4095;

    if (mprotect((void *)code_page, 4096, 7)) return 4;
    __atomic_store_n(branch, AARCH64_NOP, __ATOMIC_RELEASE);
    __builtin___clear_cache((char *)branch, (char *)branch + sizeof(*branch));
    if (mprotect((void *)code_page, 4096, 5)) {
        __atomic_store_n(branch, ORIGINAL_ESHUTTER_BRANCH, __ATOMIC_RELEASE);
        __builtin___clear_cache((char *)branch, (char *)branch + sizeof(*branch));
        mprotect((void *)code_page, 4096, 5);
        return 5;
    }

    if (mprotect((void *)table_page, 4096, 3)) {
        if (!mprotect((void *)code_page, 4096, 7)) {
            __atomic_store_n(branch, ORIGINAL_ESHUTTER_BRANCH, __ATOMIC_RELEASE);
            __builtin___clear_cache((char *)branch, (char *)branch + sizeof(*branch));
            mprotect((void *)code_page, 4096, 5);
        }
        return 6;
    }
    __atomic_store_n(&table[ENABLED_MODES_SLOT / 8], (U)enabled_modes, __ATOMIC_RELEASE);
    if (mprotect((void *)table_page, 4096, 1)) {
        __atomic_store_n(&table[ENABLED_MODES_SLOT / 8], (U)original_enabled_modes, __ATOMIC_RELEASE);
        mprotect((void *)table_page, 4096, 1);
        if (!mprotect((void *)code_page, 4096, 7)) {
            __atomic_store_n(branch, ORIGINAL_ESHUTTER_BRANCH, __ATOMIC_RELEASE);
            __builtin___clear_cache((char *)branch, (char *)branch + sizeof(*branch));
            mprotect((void *)code_page, 4096, 5);
        }
        return 7;
    }
    return 0;
}

__attribute__((constructor)) static void install(void) {
    char path[128];
    long n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0 || n >= (long)sizeof(path) - 1) return;
    path[n] = 0;
    if (!equal(path, "/system/bin/camera-service")) return;
    dl_iterate_phdr(main_base, 0);
    if (!base) { report(1); return; }
    report(attach());
}
