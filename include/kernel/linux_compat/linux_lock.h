#ifndef KERNEL_LINUX_COMPAT_LINUX_LOCK_H
#define KERNEL_LINUX_COMPAT_LINUX_LOCK_H

/* Linux ABI advisory file lock + kernel copy_file_range syscalls.
 *
 *   int     flock(int fd, int operation);
 *   ssize_t copy_file_range(int fd_in, off_t *off_in,
 *                            int fd_out, off_t *off_out,
 *                            size_t len, unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox profile lock (`profile/.parentlock`) is created
 *     and held via flock(LOCK_EX | LOCK_NB). -ENOSYS makes
 *     Firefox think another process has the profile open and
 *     refuses to start.
 *   - SQLite uses flock as a fallback when fcntl record locks
 *     fail (very common on tmpfs).
 *   - Firefox cache uses copy_file_range to deduplicate cache
 *     entries efficiently. -ENOSYS forces userspace read+write
 *     fallback.
 *
 * Marco M1 has no shared filesystem state to lock against
 * (single-process world). flock is implemented as a per-fd
 * advisory state machine: holding LOCK_EX or LOCK_SH on an
 * fd is a no-op since there are no other holders. We track
 * the "current state" in a small lock table so that callers
 * doing flock-then-flock-then-unlock get the right return
 * codes.
 *
 * copy_file_range is exposed as -ENOSYS by default (musl/
 * Firefox handle the fallback) or wired to a provider that
 * implements it (e.g. tmpfs intra-fs copy when both fds
 * are tmpfs). */

#include <stdint.h>
#include <stddef.h>

/* flock(2) operation flags. */
#define LINUX_LOCK_SH        1
#define LINUX_LOCK_EX        2
#define LINUX_LOCK_UN        8
#define LINUX_LOCK_NB     0x04

#define LINUX_LOCK_MODE_MASK   (LINUX_LOCK_SH | LINUX_LOCK_EX | LINUX_LOCK_UN)
#define LINUX_LOCK_KNOWN_FLAGS (LINUX_LOCK_MODE_MASK | LINUX_LOCK_NB)

struct linux_lock_ops {
    /* Optional callback for copy_file_range. NULL = -ENOSYS so
     * userland falls back. The callback is responsible for
     * advancing each offset (when non-NULL) and returning the
     * number of bytes copied (or a negative errno). */
    int64_t (*copy_file_range)(int fd_in, int64_t *off_in,
                               int fd_out, int64_t *off_out,
                               size_t len, uint32_t flags);
};

void linux_lock_install_ops(const struct linux_lock_ops *ops);
void linux_lock_reset_for_tests(void);

int64_t linux_flock(int fd, int operation);

int64_t linux_copy_file_range(int fd_in, int64_t *off_in,
                              int fd_out, int64_t *off_out,
                              size_t len, uint32_t flags);

void linux_lock_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_LOCK_H */
