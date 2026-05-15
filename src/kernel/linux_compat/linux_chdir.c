#include "kernel/linux_compat/linux_chdir.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_chdir_ops g_ops;
static int                    g_ops_installed;

void linux_chdir_install_ops(const struct linux_chdir_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_chdir_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_chdir_reset_for_tests(void) {
    g_ops = (struct linux_chdir_ops){0};
    g_ops_installed = 0;
}

int64_t linux_chdir(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    if (g_ops_installed && g_ops.chdir_path) {
        return g_ops.chdir_path(path);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fchdir(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.chdir_fd) {
        return g_ops.chdir_fd(fd);
    }
    return -LINUX_ENOSYS;
}

static int64_t sys_chdir(const struct linux_syscall_args *a) {
    return linux_chdir((const char *)(uintptr_t)a->a0);
}
static int64_t sys_fchdir(const struct linux_syscall_args *a) {
    return linux_fchdir((int)a->a0);
}

void linux_chdir_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_chdir,  sys_chdir);
    (void)linux_syscall_register(LINUX_NR_fchdir, sys_fchdir);
}
