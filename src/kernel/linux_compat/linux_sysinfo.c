#include "kernel/linux_compat/linux_sysinfo.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_sysinfo_providers g_providers;
static int                            g_providers_installed;

static void zero_buf(void *p, size_t n) {
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

void linux_sysinfo_install(const struct linux_sysinfo_providers *p) {
    if (!p) {
        g_providers = (struct linux_sysinfo_providers){0};
        g_providers_installed = 0;
        return;
    }
    g_providers = *p;
    g_providers_installed = 1;
}

void linux_sysinfo_reset_for_tests(void) {
    g_providers = (struct linux_sysinfo_providers){0};
    g_providers_installed = 0;
}

int64_t linux_sysinfo(struct linux_sysinfo *info) {
    if (!info) return -LINUX_EFAULT;
    zero_buf(info, sizeof(*info));

    info->mem_unit = 1; /* report bytes verbatim */
    info->procs    = 1; /* default Marco M1: single task */

    if (g_providers_installed) {
        if (g_providers.total_ram_bytes) {
            info->totalram = g_providers.total_ram_bytes();
        }
        if (g_providers.free_ram_bytes) {
            info->freeram = g_providers.free_ram_bytes();
        }
        if (g_providers.uptime_seconds) {
            info->uptime = g_providers.uptime_seconds();
        }
        if (g_providers.nproc) {
            info->procs = g_providers.nproc();
        }
    }
    return 0;
}

int64_t linux_getrusage(int who, struct linux_rusage *usage) {
    if (!usage) return -LINUX_EFAULT;
    if (who != LINUX_RUSAGE_SELF &&
        who != LINUX_RUSAGE_CHILDREN &&
        who != LINUX_RUSAGE_THREAD) {
        return -LINUX_EINVAL;
    }
    /* Marco M1: no per-task accounting yet. Zero everything;
     * userland (musl, glibc, time(1)) reads the zeroes as
     * "no usage recorded" rather than aborting. */
    zero_buf(usage, sizeof(*usage));
    return 0;
}

static int64_t sys_sysinfo(const struct linux_syscall_args *a) {
    return linux_sysinfo((struct linux_sysinfo *)(uintptr_t)a->a0);
}

static int64_t sys_getrusage(const struct linux_syscall_args *a) {
    return linux_getrusage((int)a->a0,
                           (struct linux_rusage *)(uintptr_t)a->a1);
}

void linux_sysinfo_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_sysinfo, sys_sysinfo);
    (void)linux_syscall_register(LINUX_NR_getrusage, sys_getrusage);
}
