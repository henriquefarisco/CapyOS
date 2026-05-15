#include "kernel/linux_compat/linux_settod.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_settod_ops g_ops;
static int                     g_ops_installed;

void linux_settod_install_ops(const struct linux_settod_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_settod_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_settod_reset_for_tests(void) {
    g_ops = (struct linux_settod_ops){0};
    g_ops_installed = 0;
}

int64_t linux_settimeofday(const struct linux_settod_timeval *tv,
                           const void *tz) {
    (void)tz; /* Linux: tz is ignored since 2.6.x. */
    if (!tv) {
        /* Linux: NULL tv with NULL tz is a no-op; NULL tv
         * with non-NULL tz used to set the timezone (now
         * ignored). We follow the kernel's modern behaviour
         * of returning 0 either way. */
        return 0;
    }
    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -LINUX_EINVAL;
    if (tv->tv_sec < 0) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.set_seconds) {
        return g_ops.set_seconds(tv->tv_sec, tv->tv_usec);
    }
    /* Marco M1 has no persistent wall-clock state; accept
     * the call as no-op success (root has CAP_SYS_TIME). */
    return 0;
}

static int64_t sys_settimeofday(const struct linux_syscall_args *a) {
    return linux_settimeofday(
        (const struct linux_settod_timeval *)(uintptr_t)a->a0,
        (const void *)(uintptr_t)a->a1);
}

void linux_settod_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_settimeofday, sys_settimeofday);
}
