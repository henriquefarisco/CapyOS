#ifndef KERNEL_LINUX_COMPAT_LINUX_PIPE_ZERO_H
#define KERNEL_LINUX_COMPAT_LINUX_PIPE_ZERO_H

/* Linux ABI pipe zero-copy syscalls.
 *
 *   ssize_t splice  (int fd_in, off_t *off_in,
 *                    int fd_out, off_t *off_out,
 *                    size_t len, unsigned int flags);
 *   ssize_t tee     (int fd_in, int fd_out,
 *                    size_t len, unsigned int flags);
 *   ssize_t vmsplice(int fd, const struct iovec *iov,
 *                    unsigned long nr_segs, unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - musl `posix_fadvise(POSIX_FADV_WILLNEED)` is sometimes
 *     implemented via splice into /dev/null (deprecated path)
 *     and tee for fan-out IO. -ENOSYS makes the implementation
 *     fall back to read+write.
 *   - Firefox cache uses splice between SOCK_STREAM and a file
 *     fd to avoid a userspace bounce when streaming downloads
 *     into the cache file.
 *   - vmsplice is used by performance-critical IPC paths when
 *     the IPC peer is local; on -ENOSYS the path silently
 *     degrades to writev.
 *
 * Marco M1 has no actual zero-copy plumbing yet. We accept
 * the calls and return -ENOSYS by default so userland's
 * fallback paths take over deterministically; a provider hook
 * lets a future tmpfs+pipe pair install actual zero-copy
 * routing. */

#include <stdint.h>
#include <stddef.h>

/* splice/tee/vmsplice flags (uapi/linux/fs.h). */
#define LINUX_SPLICE_F_MOVE     0x01u
#define LINUX_SPLICE_F_NONBLOCK 0x02u
#define LINUX_SPLICE_F_MORE     0x04u
#define LINUX_SPLICE_F_GIFT     0x08u

#define LINUX_SPLICE_F_KNOWN \
    (LINUX_SPLICE_F_MOVE | LINUX_SPLICE_F_NONBLOCK | \
     LINUX_SPLICE_F_MORE | LINUX_SPLICE_F_GIFT)

struct linux_pipe_iovec {
    void   *iov_base;
    size_t  iov_len;
};

#define LINUX_PIPE_ZERO_IOV_MAX 1024

struct linux_pipe_zero_ops {
    /* Optional callback for splice. NULL = -ENOSYS so userland
     * falls back. */
    int64_t (*splice)(int fd_in, int64_t *off_in,
                      int fd_out, int64_t *off_out,
                      size_t len, uint32_t flags);
    int64_t (*tee)(int fd_in, int fd_out,
                   size_t len, uint32_t flags);
    int64_t (*vmsplice)(int fd, const struct linux_pipe_iovec *iov,
                        size_t nr_segs, uint32_t flags);
};

void linux_pipe_zero_install_ops(const struct linux_pipe_zero_ops *ops);
void linux_pipe_zero_reset_for_tests(void);

int64_t linux_splice  (int fd_in, int64_t *off_in,
                       int fd_out, int64_t *off_out,
                       size_t len, uint32_t flags);
int64_t linux_tee     (int fd_in, int fd_out,
                       size_t len, uint32_t flags);
int64_t linux_vmsplice(int fd, const struct linux_pipe_iovec *iov,
                       size_t nr_segs, uint32_t flags);

void linux_pipe_zero_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PIPE_ZERO_H */
