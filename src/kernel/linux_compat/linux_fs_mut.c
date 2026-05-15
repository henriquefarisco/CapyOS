#include "kernel/linux_compat/linux_fs_mut.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_fs_mut_ops g_ops;
static int                     g_ops_installed;

void linux_fs_mut_install_ops(const struct linux_fs_mut_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_fs_mut_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_fs_mut_reset_for_tests(void) {
    g_ops = (struct linux_fs_mut_ops){0};
    g_ops_installed = 0;
}

static int64_t validate_path(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return 0;
}

/* Common dirfd handling: only AT_FDCWD is honoured today. Real
 * directory fds will require a directory-entry table that does
 * not yet exist. */
static int dirfd_ok(int dirfd) {
    return dirfd == LINUX_FS_MUT_AT_FDCWD;
}

/* --- mkdir / mkdirat -------------------------------------------- */

int64_t linux_mkdir(const char *path, uint32_t mode) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    /* Linux: mode bits above 07777 silently ignored. mkdir applies
     * umask before passing the mode through so the kernel only sees
     * 12 bits anyway, but we don't reject high bits explicitly. */
    if (g_ops_installed && g_ops.mkdir) {
        return g_ops.mkdir(path, mode & 07777);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_mkdirat(int dirfd, const char *path, uint32_t mode) {
    if (!dirfd_ok(dirfd)) return -LINUX_ENOTDIR;
    return linux_mkdir(path, mode);
}

/* --- rmdir ------------------------------------------------------ */

int64_t linux_rmdir(const char *path) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (g_ops_installed && g_ops.rmdir) {
        return g_ops.rmdir(path);
    }
    return -LINUX_ENOSYS;
}

/* --- unlink / unlinkat ------------------------------------------ */

int64_t linux_unlink(const char *path) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (g_ops_installed && g_ops.unlink) {
        return g_ops.unlink(path);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_unlinkat(int dirfd, const char *path, int flags) {
    if (!dirfd_ok(dirfd)) return -LINUX_ENOTDIR;
    if (flags & ~LINUX_AT_REMOVEDIR) return -LINUX_EINVAL;
    if (flags & LINUX_AT_REMOVEDIR) {
        return linux_rmdir(path);
    }
    return linux_unlink(path);
}

/* --- rename / renameat / renameat2 ------------------------------ */

int64_t linux_renameat2(int oldfd, const char *old, int newfd,
                        const char *new, uint32_t flags) {
    if (!dirfd_ok(oldfd) || !dirfd_ok(newfd)) return -LINUX_ENOTDIR;
    if (flags & ~LINUX_RENAME_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* NOREPLACE and EXCHANGE are mutually exclusive (Linux source:
     * fs/namei.c renameat2 returns -EINVAL for the combo). */
    if ((flags & LINUX_RENAME_NOREPLACE) &&
        (flags & LINUX_RENAME_EXCHANGE)) {
        return -LINUX_EINVAL;
    }
    int64_t rc = validate_path(old);
    if (rc) return rc;
    rc = validate_path(new);
    if (rc) return rc;
    if (g_ops_installed && g_ops.rename) {
        return g_ops.rename(old, new, flags);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_renameat(int oldfd, const char *old, int newfd,
                       const char *new) {
    return linux_renameat2(oldfd, old, newfd, new, 0);
}

int64_t linux_rename(const char *oldpath, const char *newpath) {
    return linux_renameat2(LINUX_FS_MUT_AT_FDCWD, oldpath,
                           LINUX_FS_MUT_AT_FDCWD, newpath, 0);
}

/* --- syscall adapters ------------------------------------------- */

static int64_t sys_mkdir(const struct linux_syscall_args *a) {
    return linux_mkdir((const char *)(uintptr_t)a->a0, (uint32_t)a->a1);
}
static int64_t sys_mkdirat(const struct linux_syscall_args *a) {
    return linux_mkdirat((int)a->a0,
                         (const char *)(uintptr_t)a->a1,
                         (uint32_t)a->a2);
}
static int64_t sys_rmdir(const struct linux_syscall_args *a) {
    return linux_rmdir((const char *)(uintptr_t)a->a0);
}
static int64_t sys_unlink(const struct linux_syscall_args *a) {
    return linux_unlink((const char *)(uintptr_t)a->a0);
}
static int64_t sys_unlinkat(const struct linux_syscall_args *a) {
    return linux_unlinkat((int)a->a0,
                          (const char *)(uintptr_t)a->a1,
                          (int)a->a2);
}
static int64_t sys_rename(const struct linux_syscall_args *a) {
    return linux_rename((const char *)(uintptr_t)a->a0,
                        (const char *)(uintptr_t)a->a1);
}
static int64_t sys_renameat(const struct linux_syscall_args *a) {
    return linux_renameat((int)a->a0,
                          (const char *)(uintptr_t)a->a1,
                          (int)a->a2,
                          (const char *)(uintptr_t)a->a3);
}
static int64_t sys_renameat2(const struct linux_syscall_args *a) {
    return linux_renameat2((int)a->a0,
                           (const char *)(uintptr_t)a->a1,
                           (int)a->a2,
                           (const char *)(uintptr_t)a->a3,
                           (uint32_t)a->a4);
}

void linux_fs_mut_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_mkdir,     sys_mkdir);
    (void)linux_syscall_register(LINUX_NR_mkdirat,   sys_mkdirat);
    (void)linux_syscall_register(LINUX_NR_rmdir,     sys_rmdir);
    (void)linux_syscall_register(LINUX_NR_unlink,    sys_unlink);
    (void)linux_syscall_register(LINUX_NR_unlinkat,  sys_unlinkat);
    (void)linux_syscall_register(LINUX_NR_rename,    sys_rename);
    (void)linux_syscall_register(LINUX_NR_renameat,  sys_renameat);
    (void)linux_syscall_register(LINUX_NR_renameat2, sys_renameat2);
}
