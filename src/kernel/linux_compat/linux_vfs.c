#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_vfs_ops g_ops;

void linux_vfs_install_ops(const struct linux_vfs_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_vfs_ops){0};
}

void linux_vfs_reset_for_tests(void) {
    g_ops = (struct linux_vfs_ops){0};
}

static size_t path_strlen(const char *p, size_t cap) {
    size_t i = 0;
    while (i < cap && p[i] != '\0') i++;
    return i;
}

/* ---------- open ---------- */

int64_t linux_vfs_open(uint64_t path_ptr, uint32_t flags, uint32_t mode) {
    if (path_ptr == 0) return -LINUX_EFAULT;
    if (flags & ~LINUX_VFS_OPEN_KNOWN_FLAGS) return -LINUX_EINVAL;

    /* O_EXCL requires O_CREAT (Linux invariant). */
    if ((flags & LINUX_VFS_O_EXCL) && !(flags & LINUX_VFS_O_CREAT)) {
        return -LINUX_EINVAL;
    }
    /* The access-mode field must be one of RDONLY/WRONLY/RDWR. */
    uint32_t acc = flags & LINUX_VFS_O_ACCMODE;
    if (acc == LINUX_VFS_O_ACCMODE) return -LINUX_EINVAL;

    const char *path = (const char *)(uintptr_t)path_ptr;
    /* Verify path is NUL-terminated within PATH_MAX. */
    if (path_strlen(path, LINUX_VFS_PATH_MAX) >= LINUX_VFS_PATH_MAX) {
        return -LINUX_ENAMETOOLONG;
    }

    if (!g_ops.open) return -LINUX_ENOSYS;
    int rc = g_ops.open(path, flags, mode);
    /* Callback contract: fd >= 0 on success, -errno on failure.
     * We pass the errno through so userland sees ENOENT vs EFAULT
     * vs EINVAL vs EMFILE distinctly. Legacy callers that returned
     * a bare -1 still get an "errno" of 1 (-LINUX_EPERM) which is
     * acceptably loud during development. */
    if (rc < 0) return (int64_t)rc;
    return (int64_t)rc;
}

int64_t linux_vfs_close(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (!g_ops.close) return -LINUX_ENOSYS;
    int rc = g_ops.close(fd);
    /* Pass through callback errno; defaults to EBADF only when the
     * callback returns a bare -1. */
    if (rc < 0) return rc == -1 ? -LINUX_EBADF : (int64_t)rc;
    return 0;
}

int64_t linux_vfs_read(int fd, uint64_t buf_ptr, size_t len) {
    if (fd < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (buf_ptr == 0) return -LINUX_EFAULT;
    if (!g_ops.read) return -LINUX_ENOSYS;
    return g_ops.read(fd, (void *)(uintptr_t)buf_ptr, len);
}

int64_t linux_vfs_write(int fd, uint64_t buf_ptr, size_t len) {
    if (fd < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (buf_ptr == 0) return -LINUX_EFAULT;
    if (!g_ops.write) return -LINUX_ENOSYS;
    return g_ops.write(fd, (const void *)(uintptr_t)buf_ptr, len);
}

int64_t linux_vfs_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0) return -LINUX_EBADF;
    if (whence != LINUX_SEEK_SET && whence != LINUX_SEEK_CUR &&
        whence != LINUX_SEEK_END && whence != LINUX_SEEK_DATA &&
        whence != LINUX_SEEK_HOLE) {
        return -LINUX_EINVAL;
    }
    if (!g_ops.lseek) return -LINUX_ENOSYS;
    return g_ops.lseek(fd, offset, whence);
}

int64_t linux_vfs_openat(int dirfd, uint64_t path_ptr,
                         uint32_t flags, uint32_t mode) {
    /* AT_FDCWD: equivalent to absolute open in our cwd-less model. */
    if (dirfd == LINUX_AT_FDCWD) {
        return linux_vfs_open(path_ptr, flags, mode);
    }
    /* dirfd refers to an opened directory; we don't expose those
     * yet. Linux returns ENOTDIR if dirfd is not a directory. */
    if (dirfd < 0) return -LINUX_EBADF;
    return -LINUX_ENOTDIR;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_open(const struct linux_syscall_args *a) {
    return linux_vfs_open(a->a0, (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_openat(const struct linux_syscall_args *a) {
    return linux_vfs_openat((int)a->a0, a->a1,
                            (uint32_t)a->a2, (uint32_t)a->a3);
}
static int64_t sys_close(const struct linux_syscall_args *a) {
    return linux_vfs_close((int)a->a0);
}
static int64_t sys_read(const struct linux_syscall_args *a) {
    return linux_vfs_read((int)a->a0, a->a1, (size_t)a->a2);
}
static int64_t sys_write(const struct linux_syscall_args *a) {
    return linux_vfs_write((int)a->a0, a->a1, (size_t)a->a2);
}
static int64_t sys_lseek(const struct linux_syscall_args *a) {
    return linux_vfs_lseek((int)a->a0, (int64_t)a->a1, (int)a->a2);
}

void linux_vfs_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_open,   sys_open);
    (void)linux_syscall_register(LINUX_NR_close,  sys_close);
    (void)linux_syscall_register(LINUX_NR_read,   sys_read);
    (void)linux_syscall_register(LINUX_NR_write,  sys_write);
    (void)linux_syscall_register(LINUX_NR_lseek,  sys_lseek);
    (void)linux_syscall_register(LINUX_NR_openat, sys_openat);
}
