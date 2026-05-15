#ifndef KERNEL_LINUX_COMPAT_LINUX_AT_H
#define KERNEL_LINUX_COMPAT_LINUX_AT_H

/* Linux ABI `access(2)` / `faccessat(2)` / `fstatat(2)` -- the
 * AT-family of path-relative syscalls plus plain `access`.
 *
 *   int access(const char *path, int mode);
 *   int faccessat(int dirfd, const char *path, int mode, int flags);
 *   int fstatat(int dirfd, const char *path,
 *               struct stat *buf, int flags);
 *
 * These are heavily used by musl, glibc, and userland tools:
 *
 *   - `access` and `faccessat` probe file existence + permissions
 *     before opening. `./configure` scripts, shell builtins, and
 *     dynamic linkers issue many of these; ENOSYS aborts most of
 *     them. We accept syntactically-valid paths and return ENOENT
 *     for unknown paths (no namei walker yet) but answer 0 for
 *     known pseudo paths mirrored from linux_stat (`/`, `/dev`,
 *     `/proc`, `/tmp`, fixed devfs nodes, fixed procfs files).
 *
 *   - `fstatat` is the AT_-family equivalent of `stat`: it stats
 *     a path relative to a directory fd. On Linux, glibc's `stat`
 *     wrapper calls `fstatat(AT_FDCWD, path, ...)`. With
 *     `AT_EMPTY_PATH` and a valid fd it reduces to `fstat`. We
 *     handle the AT_EMPTY_PATH form by projecting the synthetic
 *     `linux_fstat` onto `struct stat`; AT_FDCWD path-based fstatat
 *     delegates to linux_stat/linux_lstat for known pseudo paths and
 *     preserves -ENOSYS for unknown paths so userland can still fall
 *     back to the open+fstat path that already works.
 *
 * The `mode` bits for access():
 *   F_OK = 0  -- existence only
 *   R_OK = 4  -- readable
 *   W_OK = 2  -- writable
 *   X_OK = 1  -- executable
 *
 * Marco M1 always answers "permitted" for known-existing paths
 * (we run as effective root in single-task mode), so any
 * combination of R_OK|W_OK|X_OK passes if the path is in our
 * known-existing set. */

#include <stdint.h>
#include <stddef.h>

struct linux_stat;

#define LINUX_AT_FDCWD       (-100)
#define LINUX_AT_EMPTY_PATH  0x1000
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_SYMLINK_FOLLOW   0x400
#define LINUX_AT_NO_AUTOMOUNT     0x800

#define LINUX_AT_F_OK 0
#define LINUX_AT_X_OK 1
#define LINUX_AT_W_OK 2
#define LINUX_AT_R_OK 4
#define LINUX_AT_MODE_MASK (LINUX_AT_F_OK | LINUX_AT_R_OK | \
                            LINUX_AT_W_OK | LINUX_AT_X_OK)

int64_t linux_access(const char *pathname, int mode);
int64_t linux_faccessat(int dirfd, const char *pathname,
                        int mode, int flags);
int64_t linux_fstatat(int dirfd, const char *pathname,
                      struct linux_stat *buf, int flags);

void linux_at_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_AT_H */
