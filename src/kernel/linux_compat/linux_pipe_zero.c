#include "kernel/linux_compat/linux_pipe_zero.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_pipe_zero_ops g_ops;
static int                        g_ops_installed;

void linux_pipe_zero_install_ops(const struct linux_pipe_zero_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_pipe_zero_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_pipe_zero_reset_for_tests(void) {
    g_ops = (struct linux_pipe_zero_ops){0};
    g_ops_installed = 0;
}

int64_t linux_splice(int fd_in, int64_t *off_in,
                     int fd_out, int64_t *off_out,
                     size_t len, uint32_t flags) {
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;
    if (flags & ~LINUX_SPLICE_F_KNOWN) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.splice) {
        return g_ops.splice(fd_in, off_in, fd_out, off_out, len, flags);
    }
    /* No backend; userland falls back to read+write. */
    return -LINUX_ENOSYS;
}

int64_t linux_tee(int fd_in, int fd_out, size_t len, uint32_t flags) {
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;
    if (flags & ~LINUX_SPLICE_F_KNOWN) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.tee) {
        return g_ops.tee(fd_in, fd_out, len, flags);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_vmsplice(int fd, const struct linux_pipe_iovec *iov,
                       size_t nr_segs, uint32_t flags) {
    if (fd < 0) return -LINUX_EBADF;
    if (flags & ~LINUX_SPLICE_F_KNOWN) return -LINUX_EINVAL;
    if (nr_segs > LINUX_PIPE_ZERO_IOV_MAX) return -LINUX_EINVAL;
    if (nr_segs == 0) return 0;
    if (!iov) return -LINUX_EFAULT;
    if (g_ops_installed && g_ops.vmsplice) {
        return g_ops.vmsplice(fd, iov, nr_segs, flags);
    }
    return -LINUX_ENOSYS;
}

static int64_t sys_splice(const struct linux_syscall_args *a) {
    return linux_splice((int)a->a0,
                        (int64_t *)(uintptr_t)a->a1,
                        (int)a->a2,
                        (int64_t *)(uintptr_t)a->a3,
                        (size_t)a->a4,
                        (uint32_t)a->a5);
}
static int64_t sys_tee(const struct linux_syscall_args *a) {
    return linux_tee((int)a->a0, (int)a->a1,
                     (size_t)a->a2, (uint32_t)a->a3);
}
static int64_t sys_vmsplice(const struct linux_syscall_args *a) {
    return linux_vmsplice((int)a->a0,
        (const struct linux_pipe_iovec *)(uintptr_t)a->a1,
        (size_t)a->a2, (uint32_t)a->a3);
}

void linux_pipe_zero_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_splice,   sys_splice);
    (void)linux_syscall_register(LINUX_NR_tee,      sys_tee);
    (void)linux_syscall_register(LINUX_NR_vmsplice, sys_vmsplice);
}
