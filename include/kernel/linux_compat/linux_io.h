#ifndef KERNEL_LINUX_COMPAT_LINUX_IO_H
#define KERNEL_LINUX_COMPAT_LINUX_IO_H

/* Linux ABI scattered/positioned I/O syscalls.
 *
 * This module wraps the existing single-buffer read/write/lseek
 * primitives (in `linux_vfs`) with the multi-buffer and
 * positioned variants that musl/glibc routinely use:
 *
 *   readv(fd, iov, iovcnt)         -- scattered read
 *   writev(fd, iov, iovcnt)        -- gathered write
 *   pread64(fd, buf, count, off)   -- read at offset (no seek)
 *   pwrite64(fd, buf, count, off)  -- write at offset (no seek)
 *
 * musl's `__stdio_write` (which backs every `fwrite`/`fputs`
 * /`printf`) calls `writev` for buffered output flush rather
 * than write, and `__stdio_read` mirrors with `readv`. Without
 * these, every printf in the JS shell would degrade to
 * unbuffered single-byte writes.
 *
 * pread64/pwrite64 are the positioned variants that file-mapping
 * and config-loader code (e.g. SpiderMonkey's source map reader)
 * uses to peek into a file without disturbing the read cursor.
 *
 * Implementation: pure wrappers over `linux_vfs_read` /
 * `linux_vfs_write` / `linux_vfs_lseek`. No new state is owned
 * by this module. The wrappers must respect Linux semantics:
 *
 * readv/writev:
 *   - iovcnt == 0 -> return 0 (no error)
 *   - iovcnt < 0 or > LINUX_IOV_MAX -> -EINVAL
 *   - iov == NULL with iovcnt > 0 -> -EFAULT
 *   - iterate; sum bytes done so far
 *   - if FIRST element fails -> return that errno
 *   - if a LATER element fails -> return bytes already done
 *     (drop the later error -- standard Linux behaviour)
 *   - if a single element returns SHORT count -> stop and
 *     return total bytes done (no further iov processing)
 *
 * pread64/pwrite64:
 *   - offset < 0 -> -EINVAL
 *   - save current position via lseek(SEEK_CUR, 0); on failure,
 *     forward errno
 *   - seek to offset via lseek(SEEK_SET); on failure forward
 *   - call read/write
 *   - restore original position via lseek(SEEK_SET, original);
 *     position-restore failure is silently ignored (matches
 *     Linux: pread/pwrite documented to NOT update the position,
 *     and a failure to restore here would lose user data we
 *     already returned). */

#include <stdint.h>
#include <stddef.h>

/* Linux x86_64 cap on iovcnt. Linux 6.x defines this as 1024. */
#define LINUX_IOV_MAX 1024

/* `struct iovec` matches Linux x86_64 layout. Element 0 is a
 * pointer to a buffer; element 1 is the size. We reproduce it
 * here to avoid pulling <sys/uio.h> in freestanding builds. */
struct linux_iovec {
    uint64_t iov_base;  /* pointer to user buffer */
    uint64_t iov_len;   /* byte count */
};

int64_t linux_readv(int fd, const struct linux_iovec *iov, int iovcnt);
int64_t linux_writev(int fd, const struct linux_iovec *iov, int iovcnt);
int64_t linux_pread64(int fd, void *buf, size_t count, int64_t offset);
int64_t linux_pwrite64(int fd, const void *buf, size_t count, int64_t offset);

void linux_io_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_IO_H */
