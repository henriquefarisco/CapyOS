#include "kernel/linux_compat/linux_openat2.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_openat2_ops g_ops;
static int                      g_ops_installed;

void linux_openat2_install_ops(const struct linux_openat2_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_openat2_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_openat2_reset_for_tests(void) {
    g_ops = (struct linux_openat2_ops){0};
    g_ops_installed = 0;
}

int64_t linux_openat2(int dirfd, const char *path,
                      const struct linux_open_how *how, size_t size) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    if (!how) return -LINUX_EFAULT;
    /* Linux: size must be at least V0 size; smaller -> -EINVAL.
     * Larger sizes are accepted; the kernel zero-extends from
     * `how->size` to whatever it understands. */
    if (size < LINUX_OPEN_HOW_SIZE_VER0) return -LINUX_EINVAL;
    if (how->resolve & ~LINUX_RESOLVE_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    /* Linux: BENEATH and IN_ROOT are mutually exclusive. */
    if ((how->resolve & LINUX_RESOLVE_BENEATH) &&
        (how->resolve & LINUX_RESOLVE_IN_ROOT)) {
        return -LINUX_EINVAL;
    }
    /* dirfd: AT_FDCWD or fd >= 0. */
    if (dirfd != LINUX_AT_FDCWD && dirfd < 0) return -LINUX_EBADF;

    if (g_ops_installed && g_ops.openat) {
        return g_ops.openat(dirfd, path, how->flags, how->mode,
                            how->resolve);
    }
    /* No backend: report -ENOSYS so userland's fallback to
     * openat() takes over. We could also accept and synthesise
     * a fake fd, but Firefox sandbox specifically probes for
     * support and the deterministic -ENOSYS path is safer. */
    return -LINUX_ENOSYS;
}

int64_t linux_faccessat2(int dirfd, const char *path, int mode,
                         int flags) {
    if (!path && !(flags & LINUX_AT_EMPTY_PATH)) return -LINUX_EFAULT;
    if (path && path[0] == '\0' && !(flags & LINUX_AT_EMPTY_PATH)) {
        return -LINUX_ENOENT;
    }
    if ((unsigned)mode & ~(unsigned)LINUX_FACCESS_MODE_KNOWN) {
        return -LINUX_EINVAL;
    }
    if ((unsigned)flags & ~(unsigned)LINUX_FACCESSAT2_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    if (dirfd != LINUX_AT_FDCWD && dirfd < 0) return -LINUX_EBADF;

    if (g_ops_installed && g_ops.faccessat) {
        return g_ops.faccessat(dirfd, path, mode, flags);
    }
    /* No backend: assume access is granted (root in single-user
     * world). Linux returns 0 for the well-formed self-access
     * case; userland that needs real ACL checks plugs the
     * provider. */
    return 0;
}

static int64_t sys_openat2(const struct linux_syscall_args *a) {
    return linux_openat2((int)a->a0,
                         (const char *)(uintptr_t)a->a1,
                         (const struct linux_open_how *)(uintptr_t)a->a2,
                         (size_t)a->a3);
}
static int64_t sys_faccessat2(const struct linux_syscall_args *a) {
    return linux_faccessat2((int)a->a0,
                            (const char *)(uintptr_t)a->a1,
                            (int)a->a2, (int)a->a3);
}

void linux_openat2_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_openat2,     sys_openat2);
    (void)linux_syscall_register(LINUX_NR_faccessat2,  sys_faccessat2);
}
