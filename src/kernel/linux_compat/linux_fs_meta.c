#include "kernel/linux_compat/linux_fs_meta.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_fs_meta_ops g_ops;
static int                      g_ops_installed;

void linux_fs_meta_install_ops(const struct linux_fs_meta_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_fs_meta_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_fs_meta_reset_for_tests(void) {
    g_ops = (struct linux_fs_meta_ops){0};
    g_ops_installed = 0;
}

static int64_t validate_path(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return 0;
}

static int dirfd_ok(int dirfd) {
    return dirfd == LINUX_FS_META_AT_FDCWD;
}

/* --- chmod / fchmod / fchmodat ----------------------------------- */

int64_t linux_chmod(const char *path, uint32_t mode) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (g_ops_installed && g_ops.chmod_path) {
        return g_ops.chmod_path(path, mode & 07777);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fchmod(int fd, uint32_t mode) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.chmod_fd) {
        return g_ops.chmod_fd(fd, mode & 07777);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fchmodat(int dirfd, const char *path, uint32_t mode, int flags) {
    if (!dirfd_ok(dirfd)) return -LINUX_ENOTDIR;
    /* Linux fchmodat does NOT accept AT_SYMLINK_NOFOLLOW (it
     * documents -ENOTSUP for that case in glibc; the kernel
     * rejects it as -EINVAL). AT_EMPTY_PATH is not part of
     * fchmodat's contract either. We reject any flag bit. */
    if (flags != 0) return -LINUX_EINVAL;
    return linux_chmod(path, mode);
}

/* --- chown / fchown / lchown / fchownat -------------------------- */

int64_t linux_chown(const char *path, uint32_t uid, uint32_t gid) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (g_ops_installed && g_ops.chown_path) {
        return g_ops.chown_path(path, uid, gid, /*follow_symlink=*/1);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fchown(int fd, uint32_t uid, uint32_t gid) {
    if (fd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.chown_fd) {
        return g_ops.chown_fd(fd, uid, gid);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_lchown(const char *path, uint32_t uid, uint32_t gid) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (g_ops_installed && g_ops.chown_path) {
        return g_ops.chown_path(path, uid, gid, /*follow_symlink=*/0);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fchownat(int dirfd, const char *path,
                       uint32_t uid, uint32_t gid, int flags) {
    if (!dirfd_ok(dirfd)) return -LINUX_ENOTDIR;
    if ((unsigned)flags & ~(unsigned)LINUX_FS_META_AT_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    if (flags & LINUX_FS_META_AT_EMPTY_PATH) {
        /* Linux: with AT_EMPTY_PATH and dirfd valid, operate on
         * the fd. We've already restricted dirfd to AT_FDCWD
         * (=cwd), which has no fd, so this combo is invalid. */
        return -LINUX_EINVAL;
    }
    int64_t rc = validate_path(path);
    if (rc) return rc;
    int follow = (flags & LINUX_FS_META_AT_SYMLINK_NOFOLLOW) ? 0 : 1;
    if (g_ops_installed && g_ops.chown_path) {
        return g_ops.chown_path(path, uid, gid, follow);
    }
    return -LINUX_ENOSYS;
}

/* --- syscall adapters ------------------------------------------- */

static int64_t sys_chmod(const struct linux_syscall_args *a) {
    return linux_chmod((const char *)(uintptr_t)a->a0, (uint32_t)a->a1);
}
static int64_t sys_fchmod(const struct linux_syscall_args *a) {
    return linux_fchmod((int)a->a0, (uint32_t)a->a1);
}
static int64_t sys_fchmodat(const struct linux_syscall_args *a) {
    return linux_fchmodat((int)a->a0,
                          (const char *)(uintptr_t)a->a1,
                          (uint32_t)a->a2,
                          (int)a->a3);
}
static int64_t sys_chown(const struct linux_syscall_args *a) {
    return linux_chown((const char *)(uintptr_t)a->a0,
                       (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_fchown(const struct linux_syscall_args *a) {
    return linux_fchown((int)a->a0, (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_lchown(const struct linux_syscall_args *a) {
    return linux_lchown((const char *)(uintptr_t)a->a0,
                        (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_fchownat(const struct linux_syscall_args *a) {
    return linux_fchownat((int)a->a0,
                          (const char *)(uintptr_t)a->a1,
                          (uint32_t)a->a2, (uint32_t)a->a3,
                          (int)a->a4);
}

void linux_fs_meta_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_chmod,    sys_chmod);
    (void)linux_syscall_register(LINUX_NR_fchmod,   sys_fchmod);
    (void)linux_syscall_register(LINUX_NR_fchmodat, sys_fchmodat);
    (void)linux_syscall_register(LINUX_NR_chown,    sys_chown);
    (void)linux_syscall_register(LINUX_NR_fchown,   sys_fchown);
    (void)linux_syscall_register(LINUX_NR_lchown,   sys_lchown);
    (void)linux_syscall_register(LINUX_NR_fchownat, sys_fchownat);
}
