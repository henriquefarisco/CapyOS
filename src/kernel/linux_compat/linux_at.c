#include "kernel/linux_compat/linux_at.h"
#include "kernel/linux_compat/linux_stat.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static int path_is_empty(const char *p) {
    return p == NULL || p[0] == '\0';
}

static int path_is_known_existing(const char *p) {
    return linux_stat_path_is_known(p);
}

int64_t linux_access(const char *pathname, int mode) {
    if (!pathname)                     return -LINUX_EFAULT;
    if (pathname[0] == '\0')           return -LINUX_ENOENT;
    if (mode & ~LINUX_AT_MODE_MASK)    return -LINUX_EINVAL;
    /* Marco M1: we run as effective root, so R/W/X bits all
     * succeed for any path we recognise as existing. Unknown
     * paths return ENOENT (no namei walker yet). */
    if (path_is_known_existing(pathname)) return 0;
    return -LINUX_ENOENT;
}

int64_t linux_faccessat(int dirfd, const char *pathname,
                        int mode, int flags) {
    /* Linux faccessat ignores AT_SYMLINK_NOFOLLOW (it always
     * follows; faccessat2 added effective-id semantics). We
     * accept the documented flags and reject unknown bits. */
    int known_flags = LINUX_AT_SYMLINK_NOFOLLOW |
                      LINUX_AT_NO_AUTOMOUNT |
                      LINUX_AT_EMPTY_PATH;
    if (flags & ~known_flags)        return -LINUX_EINVAL;
    if (mode & ~LINUX_AT_MODE_MASK)  return -LINUX_EINVAL;
    if (!pathname)                   return -LINUX_EFAULT;

    /* AT_EMPTY_PATH with empty path probes the dirfd itself.
     * In Marco M1 any non-negative fd is "open" by virtue of
     * being a valid encoded fd, so probe succeeds. */
    if (path_is_empty(pathname) && (flags & LINUX_AT_EMPTY_PATH)) {
        if (dirfd < 0) return -LINUX_EBADF;
        return 0;
    }
    if (path_is_empty(pathname))     return -LINUX_ENOENT;

    if (dirfd != LINUX_AT_FDCWD && dirfd < 0) return -LINUX_EBADF;
    if (dirfd != LINUX_AT_FDCWD && dirfd >= 0) {
        return -LINUX_ENOTDIR;
    }
    /* AT_FDCWD: same as plain access. */
    if (path_is_known_existing(pathname)) return 0;
    return -LINUX_ENOENT;
}

int64_t linux_fstatat(int dirfd, const char *pathname,
                      struct linux_stat *buf, int flags) {
    if (!buf) return -LINUX_EFAULT;

    int known_flags = LINUX_AT_SYMLINK_NOFOLLOW |
                      LINUX_AT_NO_AUTOMOUNT |
                      LINUX_AT_EMPTY_PATH;
    if (flags & ~known_flags) return -LINUX_EINVAL;

    /* AT_EMPTY_PATH path: stat the dirfd itself (= fstat). */
    int empty = path_is_empty(pathname);
    if (empty && (flags & LINUX_AT_EMPTY_PATH)) {
        if (dirfd < 0) return -LINUX_EBADF;
        return linux_fstat(dirfd, buf);
    }
    /* Some musl paths issue fstatat(fd, "", 0, &buf) without
     * AT_EMPTY_PATH; tolerate when dirfd is non-negative. */
    if (empty && dirfd >= 0) {
        return linux_fstat(dirfd, buf);
    }
    if (empty) return -LINUX_ENOENT;

    if (dirfd != LINUX_AT_FDCWD && dirfd < 0) return -LINUX_EBADF;
    if (dirfd != LINUX_AT_FDCWD && dirfd >= 0) {
        return -LINUX_ENOTDIR;
    }
    if (flags & LINUX_AT_SYMLINK_NOFOLLOW) {
        return linux_lstat(pathname, buf);
    }
    return linux_stat(pathname, buf);
}

static int64_t sys_access(const struct linux_syscall_args *a) {
    return linux_access((const char *)(uintptr_t)a->a0, (int)a->a1);
}

static int64_t sys_faccessat(const struct linux_syscall_args *a) {
    return linux_faccessat((int)a->a0,
                           (const char *)(uintptr_t)a->a1,
                           (int)a->a2,
                           (int)a->a3);
}

static int64_t sys_fstatat(const struct linux_syscall_args *a) {
    return linux_fstatat((int)a->a0,
                         (const char *)(uintptr_t)a->a1,
                         (struct linux_stat *)(uintptr_t)a->a2,
                         (int)a->a3);
}

void linux_at_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_access, sys_access);
    (void)linux_syscall_register(LINUX_NR_faccessat, sys_faccessat);
    (void)linux_syscall_register(LINUX_NR_fstatat, sys_fstatat);
}
