#include "kernel/linux_compat/linux_link.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_link_ops g_ops;
static int                   g_ops_installed;

void linux_link_install_ops(const struct linux_link_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_link_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_link_reset_for_tests(void) {
    g_ops = (struct linux_link_ops){0};
    g_ops_installed = 0;
}

static int64_t validate_path(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return 0;
}

static int dirfd_ok(int dirfd) {
    return dirfd == LINUX_LINK_AT_FDCWD;
}

/* --- link / linkat ---------------------------------------------- */

int64_t linux_linkat(int olddirfd, const char *oldpath,
                     int newdirfd, const char *newpath, int flags) {
    if (!dirfd_ok(olddirfd) || !dirfd_ok(newdirfd)) {
        return -LINUX_ENOTDIR;
    }
    if ((unsigned)flags & ~(unsigned)LINUX_LINK_AT_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    int64_t rc = validate_path(oldpath);
    if (rc) return rc;
    rc = validate_path(newpath);
    if (rc) return rc;
    int follow = (flags & LINUX_LINK_AT_SYMLINK_FOLLOW) ? 1 : 0;
    if (g_ops_installed && g_ops.hard_link) {
        return g_ops.hard_link(oldpath, newpath, follow);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_link(const char *oldpath, const char *newpath) {
    return linux_linkat(LINUX_LINK_AT_FDCWD, oldpath,
                        LINUX_LINK_AT_FDCWD, newpath, 0);
}

/* --- symlink / symlinkat ---------------------------------------- */

int64_t linux_symlinkat(const char *target, int newdirfd,
                        const char *linkpath) {
    if (!dirfd_ok(newdirfd)) return -LINUX_ENOTDIR;
    /* Linux: target must be non-NULL and non-empty (else -ENOENT
     * for symlink with empty path; -EFAULT for NULL). */
    int64_t rc = validate_path(target);
    if (rc) return rc;
    rc = validate_path(linkpath);
    if (rc) return rc;
    if (g_ops_installed && g_ops.sym_link) {
        return g_ops.sym_link(target, linkpath);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_symlink(const char *target, const char *linkpath) {
    return linux_symlinkat(target, LINUX_LINK_AT_FDCWD, linkpath);
}

/* --- syscall adapters ------------------------------------------- */

static int64_t sys_link(const struct linux_syscall_args *a) {
    return linux_link((const char *)(uintptr_t)a->a0,
                      (const char *)(uintptr_t)a->a1);
}
static int64_t sys_linkat(const struct linux_syscall_args *a) {
    return linux_linkat((int)a->a0,
                        (const char *)(uintptr_t)a->a1,
                        (int)a->a2,
                        (const char *)(uintptr_t)a->a3,
                        (int)a->a4);
}
static int64_t sys_symlink(const struct linux_syscall_args *a) {
    return linux_symlink((const char *)(uintptr_t)a->a0,
                         (const char *)(uintptr_t)a->a1);
}
static int64_t sys_symlinkat(const struct linux_syscall_args *a) {
    return linux_symlinkat((const char *)(uintptr_t)a->a0,
                           (int)a->a1,
                           (const char *)(uintptr_t)a->a2);
}

void linux_link_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_link,      sys_link);
    (void)linux_syscall_register(LINUX_NR_linkat,    sys_linkat);
    (void)linux_syscall_register(LINUX_NR_symlink,   sys_symlink);
    (void)linux_syscall_register(LINUX_NR_symlinkat, sys_symlinkat);
}
