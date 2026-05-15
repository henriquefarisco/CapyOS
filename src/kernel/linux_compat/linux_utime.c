#include "kernel/linux_compat/linux_utime.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_utime_ops g_ops;
static int                    g_ops_installed;

void linux_utime_install_ops(const struct linux_utime_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_utime_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_utime_reset_for_tests(void) {
    g_ops = (struct linux_utime_ops){0};
    g_ops_installed = 0;
}

static int64_t validate_path(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return 0;
}

static int dirfd_ok_or_fd(int dirfd) {
    /* utimensat allows AT_FDCWD or any fd >= 0 for the
     * NULL-path form. Other negative values rejected. */
    return dirfd == LINUX_UTIME_AT_FDCWD || dirfd >= 0;
}

static int dirfd_at_fdcwd(int dirfd) {
    return dirfd == LINUX_UTIME_AT_FDCWD;
}

static int valid_nsec(int64_t ns) {
    if (ns == LINUX_UTIME_NOW || ns == LINUX_UTIME_OMIT) return 1;
    return ns >= 0 && ns < 1000000000L;
}

/* Canonicalise the (atime, mtime) pair: expand UTIME_NOW to the
 * current wall-clock time. UTIME_OMIT is left as-is and the
 * provider is expected to skip the corresponding side. Returns 0
 * on success, -EINVAL on a malformed nsec field. If both are
 * UTIME_OMIT, sets *both_omit so the caller can short-circuit
 * with success. */
static int64_t canonicalise(const struct linux_timespec *src_a,
                            const struct linux_timespec *src_m,
                            struct linux_timespec *out_a,
                            struct linux_timespec *out_m,
                            int *both_omit) {
    *both_omit = 0;
    if (src_a) {
        if (!valid_nsec(src_a->tv_nsec)) return -LINUX_EINVAL;
        *out_a = *src_a;
    } else {
        out_a->tv_sec = 0;
        out_a->tv_nsec = LINUX_UTIME_NOW;
    }
    if (src_m) {
        if (!valid_nsec(src_m->tv_nsec)) return -LINUX_EINVAL;
        *out_m = *src_m;
    } else {
        out_m->tv_sec = 0;
        out_m->tv_nsec = LINUX_UTIME_NOW;
    }
    if (out_a->tv_nsec == LINUX_UTIME_OMIT &&
        out_m->tv_nsec == LINUX_UTIME_OMIT) {
        *both_omit = 1;
        return 0;
    }
    /* Expand UTIME_NOW. */
    if (out_a->tv_nsec == LINUX_UTIME_NOW ||
        out_m->tv_nsec == LINUX_UTIME_NOW) {
        struct linux_timespec now = { 0, 0 };
        if (g_ops_installed && g_ops.now) {
            g_ops.now(&now);
        }
        if (out_a->tv_nsec == LINUX_UTIME_NOW) *out_a = now;
        if (out_m->tv_nsec == LINUX_UTIME_NOW) *out_m = now;
    }
    return 0;
}

int64_t linux_utimensat(int dirfd, const char *path,
                        const struct linux_timespec *times, int flags) {
    /* utimensat(AT_FDCWD, NULL, ...) is invalid; NULL path
     * requires a real fd to operate on. */
    if (!path && dirfd_at_fdcwd(dirfd)) return -LINUX_EFAULT;
    if (!dirfd_ok_or_fd(dirfd)) return -LINUX_EBADF;
    if ((unsigned)flags & ~(unsigned)LINUX_UTIME_AT_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    if (path) {
        int64_t rc = validate_path(path);
        if (rc) return rc;
        if (!dirfd_at_fdcwd(dirfd)) return -LINUX_ENOTDIR;
    }
    struct linux_timespec a, m;
    int both_omit = 0;
    int64_t rc = canonicalise(times ? &times[0] : NULL,
                              times ? &times[1] : NULL,
                              &a, &m, &both_omit);
    if (rc) return rc;
    if (both_omit) return 0;

    if (path) {
        int follow = (flags & LINUX_UTIME_AT_SYMLINK_NOFOLLOW) ? 0 : 1;
        if (g_ops_installed && g_ops.utime_path) {
            return g_ops.utime_path(path, &a, &m, follow);
        }
        return -LINUX_ENOSYS;
    }
    /* NULL path: operate on dirfd. */
    if (g_ops_installed && g_ops.utime_fd) {
        return g_ops.utime_fd(dirfd, &a, &m);
    }
    return -LINUX_ENOSYS;
}

/* utime/utimes/futimesat: legacy paths. We accept them and
 * delegate to the provider with a "current time" timestamp.
 * Userland that genuinely needs sub-second precision uses
 * utimensat directly. The legacy buf shape is intentionally
 * abstract here -- userland passes either NULL (= "now for both")
 * or a populated buffer; we treat NULL identically to utimensat
 * with NULL times, and we treat non-NULL as "opaque, refuse" for
 * now (provider can re-implement when tmpfs metadata lands). */

int64_t linux_utime(const char *path, const void *utimbuf) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (!utimbuf) {
        /* "now for both" form. */
        return linux_utimensat(LINUX_UTIME_AT_FDCWD, path, NULL, 0);
    }
    /* Legacy `struct utimbuf` decoding requires marshalling that
     * the provider owns. Until tmpfs metadata lands, refuse with
     * -ENOSYS so userland falls back to utimensat. */
    return -LINUX_ENOSYS;
}

int64_t linux_utimes(const char *path, const void *timeval2) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (!timeval2) {
        return linux_utimensat(LINUX_UTIME_AT_FDCWD, path, NULL, 0);
    }
    return -LINUX_ENOSYS;
}

int64_t linux_futimesat(int dirfd, const char *path, const void *timeval2) {
    if (!dirfd_at_fdcwd(dirfd)) return -LINUX_ENOTDIR;
    int64_t rc = validate_path(path);
    if (rc) return rc;
    if (!timeval2) {
        return linux_utimensat(LINUX_UTIME_AT_FDCWD, path, NULL, 0);
    }
    return -LINUX_ENOSYS;
}

static int64_t sys_utime(const struct linux_syscall_args *a) {
    return linux_utime((const char *)(uintptr_t)a->a0,
                       (const void *)(uintptr_t)a->a1);
}
static int64_t sys_utimes(const struct linux_syscall_args *a) {
    return linux_utimes((const char *)(uintptr_t)a->a0,
                        (const void *)(uintptr_t)a->a1);
}
static int64_t sys_futimesat(const struct linux_syscall_args *a) {
    return linux_futimesat((int)a->a0,
                           (const char *)(uintptr_t)a->a1,
                           (const void *)(uintptr_t)a->a2);
}
static int64_t sys_utimensat(const struct linux_syscall_args *a) {
    return linux_utimensat((int)a->a0,
                           (const char *)(uintptr_t)a->a1,
                           (const struct linux_timespec *)(uintptr_t)a->a2,
                           (int)a->a3);
}

void linux_utime_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_utime,     sys_utime);
    (void)linux_syscall_register(LINUX_NR_utimes,    sys_utimes);
    (void)linux_syscall_register(LINUX_NR_futimesat, sys_futimesat);
    (void)linux_syscall_register(LINUX_NR_utimensat, sys_utimensat);
}
