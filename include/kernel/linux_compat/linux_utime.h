#ifndef KERNEL_LINUX_COMPAT_LINUX_UTIME_H
#define KERNEL_LINUX_COMPAT_LINUX_UTIME_H

/* Linux ABI timestamp mutation syscalls.
 *
 *   int utime    (const char *path, const struct utimbuf *times);
 *   int utimes   (const char *path, const struct timeval times[2]);
 *   int futimesat(int dirfd, const char *path, const struct timeval times[2]);
 *   int utimensat(int dirfd, const char *path,
 *                 const struct timespec times[2], int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox cache stores HTTP `Last-Modified` headers as the file
 *     mtime via utimensat(fd, NULL, ts, 0); the cache index uses
 *     mtime to detect stale entries. Without it, every navigation
 *     re-fetches.
 *   - musl's `futimens(fd, ts)` is implemented as
 *     `utimensat(fd, NULL, ts, 0)` (Linux x86_64 has no separate
 *     `futimens` syscall).
 *   - autoconf-generated `./configure` scripts, tar(1), cp(1) all
 *     touch utimensat constantly; -ENOSYS short-circuits them.
 *
 * Marco M1 has no namei walker yet, so path-based forms return
 * -ENOSYS unless a provider is installed. The fd-based form
 * (utimensat with NULL path) routes to the fd provider.
 *
 * Linux special timestamp values honoured by the kernel and
 * forwarded to the provider:
 *   - tv_nsec == UTIME_NOW (1<<30 - 1)   -> "set to current time"
 *   - tv_nsec == UTIME_OMIT (1<<30 - 2)  -> "leave unchanged"
 * When BOTH timestamps are UTIME_OMIT, Linux returns 0 without
 * touching anything; we replicate that fast path before calling
 * the provider. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_UTIME_AT_FDCWD             (-100)
#define LINUX_UTIME_AT_SYMLINK_NOFOLLOW  0x100
#define LINUX_UTIME_AT_EMPTY_PATH        0x1000

#define LINUX_UTIME_AT_KNOWN_FLAGS \
    (LINUX_UTIME_AT_SYMLINK_NOFOLLOW | LINUX_UTIME_AT_EMPTY_PATH)

/* Linux <linux/stat.h> sentinels for utimensat tv_nsec. */
#define LINUX_UTIME_NOW   ((1L << 30) - 1L)
#define LINUX_UTIME_OMIT  ((1L << 30) - 2L)

struct linux_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct linux_utime_ops {
    /* Path-based callback. Both timestamps already canonicalised
     * (UTIME_NOW resolved against the current clock by the
     * caller). NULL = -ENOSYS fallback. */
    int64_t (*utime_path)(const char *path,
                          const struct linux_timespec *atime,
                          const struct linux_timespec *mtime,
                          int follow_symlink);
    int64_t (*utime_fd)(int fd,
                        const struct linux_timespec *atime,
                        const struct linux_timespec *mtime);
    /* Returns the current wall-clock time as a `linux_timespec`.
     * Used to expand UTIME_NOW. May be NULL; in that case we
     * fall back to {0, 0} (epoch) and the test fixture can
     * inject a deterministic clock. */
    void (*now)(struct linux_timespec *out);
};

void linux_utime_install_ops(const struct linux_utime_ops *ops);
void linux_utime_reset_for_tests(void);

int64_t linux_utime    (const char *path, const void *utimbuf);
int64_t linux_utimes   (const char *path, const void *timeval2);
int64_t linux_futimesat(int dirfd, const char *path, const void *timeval2);
int64_t linux_utimensat(int dirfd, const char *path,
                        const struct linux_timespec *times, int flags);

void linux_utime_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_UTIME_H */
