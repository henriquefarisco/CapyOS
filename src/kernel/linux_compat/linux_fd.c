#include "kernel/linux_compat/linux_fd.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_fd_ops g_ops;

void linux_fd_install_ops(const struct linux_fd_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_fd_ops){0};
}

void linux_fd_reset_for_tests(void) {
    g_ops = (struct linux_fd_ops){0};
}

int64_t linux_pipe2(int *fds_out, uint32_t flags) {
    if (flags & ~(uint32_t)LINUX_PIPE2_KNOWN_FLAGS) return -LINUX_EINVAL;
    if (!fds_out) return -LINUX_EFAULT;
    if (!g_ops.pipe_create) return -LINUX_ENOSYS;

    int local[2] = {-1, -1};
    int rc = g_ops.pipe_create(local);
    if (rc != 0) return -LINUX_EMFILE;

    if (g_ops.set_fd_flags) {
        g_ops.set_fd_flags(local[0], flags);
        g_ops.set_fd_flags(local[1], flags);
    }

    fds_out[0] = local[0];
    fds_out[1] = local[1];
    return 0;
}

int64_t linux_pipe(int *fds_out) {
    return linux_pipe2(fds_out, 0);
}

int64_t linux_dup3(int oldfd, int newfd, uint32_t flags) {
    if (flags & ~(uint32_t)LINUX_DUP3_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* Linux 6.x: dup3 with oldfd == newfd is -EINVAL (this is the
     * key behaviour difference from dup2, which silently succeeds). */
    if (oldfd == newfd) return -LINUX_EINVAL;
    if (oldfd < 0 || newfd < 0) return -LINUX_EBADF;
    if (!g_ops.dup3) return -LINUX_ENOSYS;

    int rc = g_ops.dup3(oldfd, newfd);
    if (rc < 0) return -LINUX_EBADF;
    if (g_ops.set_fd_flags) g_ops.set_fd_flags(rc, flags);
    return (int64_t)rc;
}

/* Syscall adapters. */

static int64_t sys_pipe(const struct linux_syscall_args *a) {
    return linux_pipe((int *)(uintptr_t)a->a0);
}

static int64_t sys_pipe2(const struct linux_syscall_args *a) {
    return linux_pipe2((int *)(uintptr_t)a->a0, (uint32_t)a->a1);
}

static int64_t sys_dup3(const struct linux_syscall_args *a) {
    return linux_dup3((int)a->a0, (int)a->a1, (uint32_t)a->a2);
}

void linux_fd_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_pipe,  sys_pipe);
    (void)linux_syscall_register(LINUX_NR_pipe2, sys_pipe2);
    (void)linux_syscall_register(LINUX_NR_dup3,  sys_dup3);
}
