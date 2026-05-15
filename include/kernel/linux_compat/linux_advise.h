#ifndef KERNEL_LINUX_COMPAT_LINUX_ADVISE_H
#define KERNEL_LINUX_COMPAT_LINUX_ADVISE_H

/* Linux ABI file-advise / preallocation / kernel-copy syscalls.
 *
 *   int     posix_fadvise(int fd, off_t offset, off_t len, int advice);
 *   int     fallocate    (int fd, int mode, off_t offset, off_t len);
 *   ssize_t sendfile     (int out_fd, int in_fd,
 *                         off_t *offset, size_t count);
 *
 * Why this matters for the Firefox port:
 *   - SQLite (places.sqlite, cookies.sqlite) calls
 *     `posix_fadvise(POSIX_FADV_RANDOM)` on its database files to
 *     hint the page cache. -ENOSYS makes SQLite skip the hint and
 *     accept the readahead penalty.
 *   - Firefox's downloader uses `fallocate` to reserve disk space
 *     up front for large downloads (avoids fragmentation). Without
 *     it, downloads still work but disk fragmentation increases.
 *   - musl uses `sendfile` to implement `splice`-style copies in
 *     some libc helpers. -ENOSYS forces a userspace read+write
 *     loop (slower but correct).
 *
 * Marco M1 has no swap and no real on-disk fs, so the hints are
 * trivially satisfied. We accept all advice values, return 0 from
 * fadvise, return -EOPNOTSUPP from fallocate (Linux convention
 * for tmpfs without preallocation support), and implement
 * sendfile via a provider that defaults to "read-then-write"
 * fallback (which userland already does when sendfile is missing,
 * so returning -ENOSYS is also acceptable).
 *
 * We choose -EOPNOTSUPP for fallocate because Linux tmpfs returns
 * exactly that on `fallocate(FALLOC_FL_PUNCH_HOLE)` etc. on
 * old kernels; userland (glibc, musl, Firefox) already has the
 * fallback path. */

#include <stdint.h>
#include <stddef.h>

/* posix_fadvise advice constants (linux/fadvise.h). */
#define LINUX_POSIX_FADV_NORMAL      0
#define LINUX_POSIX_FADV_RANDOM      1
#define LINUX_POSIX_FADV_SEQUENTIAL  2
#define LINUX_POSIX_FADV_WILLNEED    3
#define LINUX_POSIX_FADV_DONTNEED    4
#define LINUX_POSIX_FADV_NOREUSE     5

#define LINUX_POSIX_FADV_MIN  LINUX_POSIX_FADV_NORMAL
#define LINUX_POSIX_FADV_MAX  LINUX_POSIX_FADV_NOREUSE

/* fallocate mode flags. */
#define LINUX_FALLOC_FL_KEEP_SIZE      0x01
#define LINUX_FALLOC_FL_PUNCH_HOLE     0x02
#define LINUX_FALLOC_FL_NO_HIDE_STALE  0x04
#define LINUX_FALLOC_FL_COLLAPSE_RANGE 0x08
#define LINUX_FALLOC_FL_ZERO_RANGE     0x10
#define LINUX_FALLOC_FL_INSERT_RANGE   0x20
#define LINUX_FALLOC_FL_UNSHARE_RANGE  0x40

#define LINUX_FALLOC_FL_KNOWN \
    (LINUX_FALLOC_FL_KEEP_SIZE | LINUX_FALLOC_FL_PUNCH_HOLE | \
     LINUX_FALLOC_FL_NO_HIDE_STALE | LINUX_FALLOC_FL_COLLAPSE_RANGE | \
     LINUX_FALLOC_FL_ZERO_RANGE | LINUX_FALLOC_FL_INSERT_RANGE | \
     LINUX_FALLOC_FL_UNSHARE_RANGE)

struct linux_advise_ops {
    /* Optional sendfile callback. NULL = -ENOSYS so userland
     * falls back to read+write. The callback is responsible for
     * advancing the offset and returning the number of bytes
     * copied (or a negative errno). */
    int64_t (*sendfile)(int out_fd, int in_fd,
                        int64_t *offset, size_t count);
};

void linux_advise_install_ops(const struct linux_advise_ops *ops);
void linux_advise_reset_for_tests(void);

int64_t linux_posix_fadvise(int fd, int64_t offset, int64_t len, int advice);
int64_t linux_fallocate    (int fd, int mode, int64_t offset, int64_t len);
int64_t linux_sendfile     (int out_fd, int in_fd,
                            int64_t *offset, size_t count);

void linux_advise_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_ADVISE_H */
