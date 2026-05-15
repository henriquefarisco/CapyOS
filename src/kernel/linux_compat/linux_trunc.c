#include "kernel/linux_compat/linux_trunc.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_trunc_ops g_ops;
static int                    g_ops_installed;

void linux_trunc_install_ops(const struct linux_trunc_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_trunc_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_trunc_reset_for_tests(void) {
    g_ops = (struct linux_trunc_ops){0};
    g_ops_installed = 0;
}

int64_t linux_truncate(const char *path, int64_t length) {
    if (!path)            return -LINUX_EFAULT;
    if (path[0] == '\0')  return -LINUX_ENOENT;
    if (length < 0)       return -LINUX_EINVAL;
    /* Path-based: needs a namei walker. Return -ENOSYS so
     * userland (musl) can fall back to open()+ftruncate(). */
    return -LINUX_ENOSYS;
}

int64_t linux_ftruncate(int fd, int64_t length) {
    if (fd < 0)     return -LINUX_EBADF;
    if (length < 0) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.ftruncate) {
        return g_ops.ftruncate(fd, length);
    }
    return -LINUX_ENOSYS;
}

static int64_t sys_truncate(const struct linux_syscall_args *a) {
    return linux_truncate((const char *)(uintptr_t)a->a0,
                          (int64_t)a->a1);
}

static int64_t sys_ftruncate(const struct linux_syscall_args *a) {
    return linux_ftruncate((int)a->a0, (int64_t)a->a1);
}

void linux_trunc_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_truncate, sys_truncate);
    (void)linux_syscall_register(LINUX_NR_ftruncate, sys_ftruncate);
}
