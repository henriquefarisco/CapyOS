#include "kernel/linux_compat/linux_caps.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_caps_ops g_ops;
static int                   g_ops_installed;

void linux_caps_install_ops(const struct linux_caps_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_caps_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_caps_reset_for_tests(void) {
    g_ops = (struct linux_caps_ops){0};
    g_ops_installed = 0;
}

static int n_entries_for_version(uint32_t version) {
    switch (version) {
        case LINUX_CAP_VERSION_1: return 1;
        case LINUX_CAP_VERSION_2: return 2;
        case LINUX_CAP_VERSION_3: return 2;
        default: return 0; /* unknown */
    }
}

int64_t linux_capget(struct linux_cap_user_header *hdrp,
                     struct linux_cap_user_data *datap) {
    if (!hdrp) return -LINUX_EFAULT;
    int n = n_entries_for_version(hdrp->version);
    if (n == 0) {
        /* Linux: kernel rewrites version field to its preferred
         * value and returns -EINVAL so userland can probe. */
        hdrp->version = LINUX_CAP_PREFERRED_VERSION;
        return -LINUX_EINVAL;
    }
    if (!datap) return -LINUX_EFAULT;
    /* Linux: pid < 0 -> -EINVAL. */
    if (hdrp->pid < 0) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.get_caps) {
        return g_ops.get_caps(hdrp->pid, datap, n);
    }
    /* Marco M1: root with all capabilities. */
    for (int i = 0; i < n; i++) {
        datap[i].effective   = 0xFFFFFFFFu;
        datap[i].permitted   = 0xFFFFFFFFu;
        datap[i].inheritable = 0xFFFFFFFFu;
    }
    return 0;
}

int64_t linux_capset(struct linux_cap_user_header *hdrp,
                     const struct linux_cap_user_data *datap) {
    if (!hdrp) return -LINUX_EFAULT;
    int n = n_entries_for_version(hdrp->version);
    if (n == 0) {
        hdrp->version = LINUX_CAP_PREFERRED_VERSION;
        return -LINUX_EINVAL;
    }
    if (!datap) return -LINUX_EFAULT;
    /* Linux: capset can only target self (pid == 0). Targeting
     * other tasks requires CAP_SETPCAP and the kernel rejects
     * them on the syscall level since Linux 2.6.25. */
    if (hdrp->pid != 0) return -LINUX_EPERM;
    if (g_ops_installed && g_ops.set_caps) {
        return g_ops.set_caps(hdrp->pid, datap, n);
    }
    /* Marco M1: no per-task cap storage; accept any value and
     * discard. Since we're root, CAP_SETPCAP is implicit. */
    return 0;
}

static int64_t sys_capget(const struct linux_syscall_args *a) {
    return linux_capget((struct linux_cap_user_header *)(uintptr_t)a->a0,
                        (struct linux_cap_user_data *)(uintptr_t)a->a1);
}
static int64_t sys_capset(const struct linux_syscall_args *a) {
    return linux_capset((struct linux_cap_user_header *)(uintptr_t)a->a0,
                        (const struct linux_cap_user_data *)(uintptr_t)a->a1);
}

void linux_caps_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_capget, sys_capget);
    (void)linux_syscall_register(LINUX_NR_capset, sys_capset);
}
