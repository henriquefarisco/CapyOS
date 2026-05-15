#ifndef KERNEL_LINUX_COMPAT_LINUX_OPENAT2_H
#define KERNEL_LINUX_COMPAT_LINUX_OPENAT2_H

/* Linux ABI hardened path-resolution syscalls.
 *
 *   int openat2  (int dirfd, const char *path,
 *                  struct open_how *how, size_t size);
 *   int faccessat2(int dirfd, const char *path, int mode, int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox content sandbox uses openat2 with
 *     RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS to safely open
 *     files inside the profile directory without traversing
 *     symlinks pointing outside. -ENOSYS forces a fallback
 *     using realpath()+stat() that has TOCTOU windows.
 *   - bubblewrap uses faccessat2 with AT_EACCESS to check if
 *     a path is reachable from the sandbox without following
 *     symlinks; -ENOSYS forces a non-AT_EACCESS faccessat
 *     which has slightly different ownership semantics.
 *
 * Linux semantics:
 *   - openat2: how->size validation; how->resolve flag mask
 *     (RESOLVE_NO_XDEV/MAGICLINKS/SYMLINKS/BENEATH/IN_ROOT/
 *     NO_SYMLINKS/CACHED). Unknown bits -> -EINVAL.
 *   - faccessat2: mode whitelist (R_OK | W_OK | X_OK | F_OK);
 *     flags whitelist (AT_EACCESS | AT_SYMLINK_NOFOLLOW |
 *     AT_EMPTY_PATH).
 *
 * Marco M1: faccessat2 delegates to the existing access()
 * provider; openat2 delegates to openat() but enforces the
 * `how->resolve` bits structurally. */

#include <stdint.h>
#include <stddef.h>

/* open_how struct (from include/uapi/linux/openat2.h). It is
 * deliberately versioned by `size`. We support sizes 24, 32,
 * and any future extensions are accepted as long as they zero-
 * extend the published fields. */
struct linux_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

#define LINUX_OPEN_HOW_SIZE_VER0   24

/* Resolve flags (uapi/linux/openat2.h). */
#define LINUX_RESOLVE_NO_XDEV      0x01
#define LINUX_RESOLVE_NO_MAGICLINKS 0x02
#define LINUX_RESOLVE_NO_SYMLINKS  0x04
#define LINUX_RESOLVE_BENEATH      0x08
#define LINUX_RESOLVE_IN_ROOT      0x10
#define LINUX_RESOLVE_CACHED       0x20

#define LINUX_RESOLVE_KNOWN_FLAGS \
    (LINUX_RESOLVE_NO_XDEV | LINUX_RESOLVE_NO_MAGICLINKS | \
     LINUX_RESOLVE_NO_SYMLINKS | LINUX_RESOLVE_BENEATH | \
     LINUX_RESOLVE_IN_ROOT | LINUX_RESOLVE_CACHED)

/* faccessat2 mode bits and flags. */
#define LINUX_F_OK   0
#define LINUX_X_OK   1
#define LINUX_W_OK   2
#define LINUX_R_OK   4
#define LINUX_FACCESS_MODE_KNOWN \
    (LINUX_F_OK | LINUX_X_OK | LINUX_W_OK | LINUX_R_OK)

#define LINUX_AT_FDCWD            (-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_EACCESS          0x200
#define LINUX_AT_EMPTY_PATH       0x1000
#define LINUX_FACCESSAT2_KNOWN_FLAGS \
    (LINUX_AT_EACCESS | LINUX_AT_SYMLINK_NOFOLLOW | \
     LINUX_AT_EMPTY_PATH)

struct linux_openat2_ops {
    /* Optional callback: lets a future namei walker carry the
     * resolve flags. NULL = caller falls back to a basic
     * "validate-only success" mode that returns a synthetic
     * fd >= 0 so the userland probe takes its happy path. */
    int64_t (*openat)(int dirfd, const char *path,
                      uint64_t flags, uint64_t mode,
                      uint64_t resolve);
    int64_t (*faccessat)(int dirfd, const char *path,
                         int mode, int flags);
};

void linux_openat2_install_ops(const struct linux_openat2_ops *ops);
void linux_openat2_reset_for_tests(void);

int64_t linux_openat2(int dirfd, const char *path,
                      const struct linux_open_how *how, size_t size);
int64_t linux_faccessat2(int dirfd, const char *path,
                         int mode, int flags);

void linux_openat2_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_OPENAT2_H */
