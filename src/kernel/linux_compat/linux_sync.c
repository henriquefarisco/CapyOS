#include "kernel/linux_compat/linux_sync.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_sync_ops g_ops;
static int                   g_ops_installed;

void linux_sync_install_ops(const struct linux_sync_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_sync_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_sync_reset_for_tests(void) {
    g_ops = (struct linux_sync_ops){0};
    g_ops_installed = 0;
}

int64_t linux_sync(void) {
    if (g_ops_installed && g_ops.sync_all) {
        return g_ops.sync_all();
    }
    /* No persistent backing store: every write is already
     * durable in RAM. */
    return 0;
}

int64_t linux_syncfs(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.sync_fs) {
        return g_ops.sync_fs(fd);
    }
    return 0;
}

int64_t linux_fsync(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.sync_fd) {
        return g_ops.sync_fd(fd, /*data_only=*/0);
    }
    return 0;
}

int64_t linux_fdatasync(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.sync_fd) {
        return g_ops.sync_fd(fd, /*data_only=*/1);
    }
    return 0;
}

static int64_t sys_sync(const struct linux_syscall_args *a) {
    (void)a;
    return linux_sync();
}
static int64_t sys_syncfs(const struct linux_syscall_args *a) {
    return linux_syncfs((int)a->a0);
}
static int64_t sys_fsync(const struct linux_syscall_args *a) {
    return linux_fsync((int)a->a0);
}
static int64_t sys_fdatasync(const struct linux_syscall_args *a) {
    return linux_fdatasync((int)a->a0);
}

void linux_sync_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_sync,      sys_sync);
    (void)linux_syscall_register(LINUX_NR_syncfs,    sys_syncfs);
    (void)linux_syscall_register(LINUX_NR_fsync,     sys_fsync);
    (void)linux_syscall_register(LINUX_NR_fdatasync, sys_fdatasync);
}
