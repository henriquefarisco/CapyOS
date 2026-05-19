#include "kernel/linux_compat/linux_advise.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_advise_ops g_ops;
static int                     g_ops_installed;

void linux_advise_install_ops(const struct linux_advise_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_advise_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_advise_reset_for_tests(void) {
    g_ops = (struct linux_advise_ops){0};
    g_ops_installed = 0;
}

int64_t linux_posix_fadvise(int fd, int64_t offset, int64_t len, int advice) {
    if (fd < 0) return -LINUX_EBADF;
    if (offset < 0 || len < 0) return -LINUX_EINVAL;
    if (advice < LINUX_POSIX_FADV_MIN || advice > LINUX_POSIX_FADV_MAX) {
        return -LINUX_EINVAL;
    }
    /* Marco M1: no page cache to hint at. The advice is purely
     * advisory under Linux; returning success is faithful. */
    return 0;
}

int64_t linux_fallocate(int fd, int mode, int64_t offset, int64_t len) {
    if (fd < 0) return -LINUX_EBADF;
    /* Linux: len must be > 0 unless PUNCH_HOLE is in mode where
     * len > 0 is still required; len <= 0 always -> -EINVAL. */
    if (len <= 0) return -LINUX_EINVAL;
    if (offset < 0) return -LINUX_EINVAL;
    if ((unsigned)mode & ~(unsigned)LINUX_FALLOC_FL_KNOWN) {
        return -LINUX_EINVAL;
    }
    /* PUNCH_HOLE requires KEEP_SIZE per Linux fs/open.c; we
     * surface that constraint to userland. */
    if ((mode & LINUX_FALLOC_FL_PUNCH_HOLE) &&
        !(mode & LINUX_FALLOC_FL_KEEP_SIZE)) {
        return -LINUX_EINVAL;
    }
    /* Marco M1: tmpfs without preallocation support. Linux tmpfs
     * returns -EOPNOTSUPP for fallocate on old kernels and
     * portable userland handles it gracefully. */
    return -LINUX_EOPNOTSUPP;
}

int64_t linux_sendfile(int out_fd, int in_fd,
                       int64_t *offset, size_t count) {
    if (out_fd < 0 || in_fd < 0) return -LINUX_EBADF;
    /* Linux: count > SSIZE_MAX is silently clamped on success;
     * we don't enforce the cap because userland has its own. */
    if (g_ops_installed && g_ops.sendfile) {
        return g_ops.sendfile(out_fd, in_fd, offset, count);
    }
    /* No backend: userland falls back to read+write. */
    return -LINUX_ENOSYS;
}

static int64_t sys_fadvise(const struct linux_syscall_args *a) {
    return linux_posix_fadvise((int)a->a0, (int64_t)a->a1,
                               (int64_t)a->a2, (int)a->a3);
}
static int64_t sys_fallocate(const struct linux_syscall_args *a) {
    return linux_fallocate((int)a->a0, (int)a->a1,
                           (int64_t)a->a2, (int64_t)a->a3);
}
static int64_t sys_sendfile(const struct linux_syscall_args *a) {
    return linux_sendfile((int)a->a0, (int)a->a1,
                          (int64_t *)(uintptr_t)a->a2,
                          (size_t)a->a3);
}

void linux_advise_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_fadvise64, sys_fadvise);
    (void)linux_syscall_register(LINUX_NR_fallocate, sys_fallocate);
    (void)linux_syscall_register(LINUX_NR_sendfile,  sys_sendfile);
}
