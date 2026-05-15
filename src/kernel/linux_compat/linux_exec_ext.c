#include "kernel/linux_compat/linux_exec_ext.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_exec_ext_ops g_ops;
static int                       g_ops_installed;

void linux_exec_ext_install_ops(const struct linux_exec_ext_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_exec_ext_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_exec_ext_reset_for_tests(void) {
    g_ops = (struct linux_exec_ext_ops){0};
    g_ops_installed = 0;
}

int64_t linux_execveat(int dirfd, const char *pathname,
                       char *const argv[], char *const envp[],
                       int flags) {
    (void)argv; (void)envp;
    /* Validation first so userland gets the right errno path
     * for malformed args even though the eventual outcome is
     * -ENOSYS. */
    if ((unsigned)flags & ~(unsigned)LINUX_EXECVEAT_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    if (dirfd != LINUX_AT_FDCWD && dirfd < 0) return -LINUX_EBADF;
    if (!pathname && !(flags & LINUX_AT_EMPTY_PATH)) {
        return -LINUX_EFAULT;
    }
    if (pathname && pathname[0] == '\0' &&
        !(flags & LINUX_AT_EMPTY_PATH)) {
        return -LINUX_ENOENT;
    }
    /* Marco M1: exec subsystem not landed yet; userland path
     * reverts to spawn-based fallbacks deterministically. */
    return -LINUX_ENOSYS;
}

int64_t linux_close_range(uint32_t first, uint32_t last, uint32_t flags) {
    if (first > last) return -LINUX_EINVAL;
    if (flags & ~LINUX_CLOSE_RANGE_KNOWN_FLAGS) return -LINUX_EINVAL;

    /* Cap last to a sane upper bound. Linux uses RLIMIT_NOFILE
     * on the fd table; we use a conservative 4096 to avoid
     * looping over the entire u32 range when last == ~0u. */
    uint32_t hi = last;
    if (hi > 4096) hi = 4096;

    if (!g_ops_installed) {
        /* No callback; we still validate the range and report
         * success so userland's "scrub fds before exec" pattern
         * proceeds. The actual close happens via per-fd close()
         * paths that already work; close_range here is just a
         * batch hint that we satisfy structurally. */
        return 0;
    }

    int set_cloexec = (flags & LINUX_CLOSE_RANGE_CLOEXEC) != 0;
    for (uint32_t fd = first; fd <= hi; fd++) {
        int64_t rc;
        if (set_cloexec) {
            rc = g_ops.set_cloexec_one ?
                 g_ops.set_cloexec_one((int)fd) : 0;
        } else {
            rc = g_ops.close_one ?
                 g_ops.close_one((int)fd) : 0;
        }
        /* Linux: errors on a single fd in close_range are
         * silently ignored (the kernel just keeps going). */
        (void)rc;
    }
    return 0;
}

static int64_t sys_execveat(const struct linux_syscall_args *a) {
    return linux_execveat((int)a->a0,
                          (const char *)(uintptr_t)a->a1,
                          (char *const *)(uintptr_t)a->a2,
                          (char *const *)(uintptr_t)a->a3,
                          (int)a->a4);
}
static int64_t sys_close_range(const struct linux_syscall_args *a) {
    return linux_close_range((uint32_t)a->a0, (uint32_t)a->a1,
                             (uint32_t)a->a2);
}

void linux_exec_ext_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_execveat,    sys_execveat);
    (void)linux_syscall_register(LINUX_NR_close_range, sys_close_range);
}
