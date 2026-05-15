#include "kernel/linux_compat/linux_rlimit_legacy.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_rlimit_legacy_ops g_ops;
static int                            g_ops_installed;

void linux_rlimit_legacy_install_ops(const struct linux_rlimit_legacy_ops *o) {
    if (!o) {
        g_ops = (struct linux_rlimit_legacy_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *o;
    g_ops_installed = 1;
}

void linux_rlimit_legacy_reset_for_tests(void) {
    g_ops = (struct linux_rlimit_legacy_ops){0};
    g_ops_installed = 0;
}

/* Marco M1 default rlimits.
 *
 * The values mirror what a 64-bit Linux kernel exposes in the
 * default initrd-friendly profile: NOFILE caps soft at 1024,
 * STACK at 8 MiB, the rest at INFINITY for things we don't
 * really limit. SpiderMonkey + bash boot cleanly with these. */
static void synthesize(int resource, struct linux_rlimit *out) {
    out->rlim_cur = LINUX_RLIM_INFINITY;
    out->rlim_max = LINUX_RLIM_INFINITY;
    switch (resource) {
        case LINUX_RLIMIT_NOFILE:
            out->rlim_cur = 1024;
            out->rlim_max = 4096;
            break;
        case LINUX_RLIMIT_STACK:
            out->rlim_cur = 8 * 1024 * 1024;
            out->rlim_max = LINUX_RLIM_INFINITY;
            break;
        case LINUX_RLIMIT_NPROC:
            out->rlim_cur = 1024;
            out->rlim_max = 1024;
            break;
        case LINUX_RLIMIT_CORE:
            out->rlim_cur = 0;
            out->rlim_max = LINUX_RLIM_INFINITY;
            break;
        default:
            break;
    }
}

int64_t linux_getrlimit(int resource, struct linux_rlimit *rlim) {
    if (resource < 0 || resource >= LINUX_RLIMIT_NLIMITS) {
        return -LINUX_EINVAL;
    }
    if (!rlim) return -LINUX_EFAULT;
    if (g_ops_installed && g_ops.get_limit) {
        return g_ops.get_limit(resource, rlim);
    }
    synthesize(resource, rlim);
    return 0;
}

int64_t linux_setrlimit(int resource, const struct linux_rlimit *rlim) {
    if (resource < 0 || resource >= LINUX_RLIMIT_NLIMITS) {
        return -LINUX_EINVAL;
    }
    if (!rlim) return -LINUX_EFAULT;
    /* Linux: rlim_cur > rlim_max -> -EINVAL. */
    if (rlim->rlim_cur != LINUX_RLIM_INFINITY &&
        rlim->rlim_max != LINUX_RLIM_INFINITY &&
        rlim->rlim_cur > rlim->rlim_max) {
        return -LINUX_EINVAL;
    }
    if (g_ops_installed && g_ops.set_limit) {
        return g_ops.set_limit(resource, rlim);
    }
    /* Marco M1: no per-task rlimit storage. We accept any
     * well-formed call as a no-op; userland code that probes
     * via getrlimit afterwards still sees the synthesised
     * defaults but its set call returned 0 (which matches
     * Linux when we have CAP_SYS_RESOURCE, which root
     * implicitly has). */
    return 0;
}

static int64_t sys_getrlimit(const struct linux_syscall_args *a) {
    return linux_getrlimit((int)a->a0,
                           (struct linux_rlimit *)(uintptr_t)a->a1);
}
static int64_t sys_setrlimit(const struct linux_syscall_args *a) {
    return linux_setrlimit((int)a->a0,
                           (const struct linux_rlimit *)(uintptr_t)a->a1);
}

void linux_rlimit_legacy_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getrlimit, sys_getrlimit);
    (void)linux_syscall_register(LINUX_NR_setrlimit, sys_setrlimit);
}
