#include "kernel/linux_compat/linux_io.h"
#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Common iov validation shared by readv/writev. */
static int64_t validate_iov(const struct linux_iovec *iov, int iovcnt) {
    if (iovcnt < 0 || iovcnt > LINUX_IOV_MAX) return -LINUX_EINVAL;
    if (iovcnt > 0 && !iov) return -LINUX_EFAULT;
    return 0;
}

int64_t linux_readv(int fd, const struct linux_iovec *iov, int iovcnt) {
    int64_t err = validate_iov(iov, iovcnt);
    if (err) return err;
    if (iovcnt == 0) return 0;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        int64_t r = linux_vfs_read(fd,
                                   iov[i].iov_base,
                                   (size_t)iov[i].iov_len);
        if (r < 0) {
            /* If we already read bytes, surface the partial
             * count and swallow the later error (Linux behaviour
             * for partial scatter reads). On the FIRST element,
             * forward the errno. */
            if (total > 0) return total;
            return r;
        }
        total += r;
        /* Short read: stop processing remaining iov elements.
         * Linux guarantees readv reports the bytes that arrived
         * but does NOT pull more from the next iov on a short
         * read. */
        if ((size_t)r < (size_t)iov[i].iov_len) return total;
    }
    return total;
}

int64_t linux_writev(int fd, const struct linux_iovec *iov, int iovcnt) {
    int64_t err = validate_iov(iov, iovcnt);
    if (err) return err;
    if (iovcnt == 0) return 0;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        int64_t r = linux_vfs_write(fd,
                                    iov[i].iov_base,
                                    (size_t)iov[i].iov_len);
        if (r < 0) {
            if (total > 0) return total;
            return r;
        }
        total += r;
        /* Short write: same as readv -- stop and report. */
        if ((size_t)r < (size_t)iov[i].iov_len) return total;
    }
    return total;
}

/* pread64/pwrite64 implementation: save position, seek, do the
 * I/O, restore position. Atomic enough for single-threaded
 * userland (Marco M1). When SMP arrives the underlying file
 * object will need a real read_at/write_at primitive that
 * doesn't perturb the cursor. */

static int64_t pread_pwrite_common(int fd, int is_write,
                                   uint64_t buf_ptr, size_t count,
                                   int64_t offset) {
    if (offset < 0) return -LINUX_EINVAL;
    /* Save the current position. If the underlying object does
     * not support seeking (pipe, socket, char device), Linux
     * returns -ESPIPE; we forward that to userland. */
    int64_t saved = linux_vfs_lseek(fd, 0, LINUX_SEEK_CUR);
    if (saved < 0) return saved;

    int64_t rc = linux_vfs_lseek(fd, offset, LINUX_SEEK_SET);
    if (rc < 0) return rc;

    int64_t io_rc;
    if (is_write) {
        io_rc = linux_vfs_write(fd, buf_ptr, count);
    } else {
        io_rc = linux_vfs_read(fd, buf_ptr, count);
    }

    /* Restore. If restore fails we still return the I/O result
     * because the data was already moved -- the position drift
     * is the lesser of two evils. */
    (void)linux_vfs_lseek(fd, saved, LINUX_SEEK_SET);
    return io_rc;
}

int64_t linux_pread64(int fd, void *buf, size_t count, int64_t offset) {
    return pread_pwrite_common(fd, 0, (uint64_t)(uintptr_t)buf,
                               count, offset);
}

int64_t linux_pwrite64(int fd, const void *buf, size_t count,
                       int64_t offset) {
    return pread_pwrite_common(fd, 1, (uint64_t)(uintptr_t)buf,
                               count, offset);
}

/* ---- Syscall adapters ---- */

static int64_t sys_readv(const struct linux_syscall_args *a) {
    return linux_readv((int)a->a0,
                       (const struct linux_iovec *)(uintptr_t)a->a1,
                       (int)a->a2);
}

static int64_t sys_writev(const struct linux_syscall_args *a) {
    return linux_writev((int)a->a0,
                        (const struct linux_iovec *)(uintptr_t)a->a1,
                        (int)a->a2);
}

static int64_t sys_pread64(const struct linux_syscall_args *a) {
    return linux_pread64((int)a->a0,
                         (void *)(uintptr_t)a->a1,
                         (size_t)a->a2,
                         (int64_t)a->a3);
}

static int64_t sys_pwrite64(const struct linux_syscall_args *a) {
    return linux_pwrite64((int)a->a0,
                          (const void *)(uintptr_t)a->a1,
                          (size_t)a->a2,
                          (int64_t)a->a3);
}

void linux_io_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_readv,    sys_readv);
    (void)linux_syscall_register(LINUX_NR_writev,   sys_writev);
    (void)linux_syscall_register(LINUX_NR_pread64,  sys_pread64);
    (void)linux_syscall_register(LINUX_NR_pwrite64, sys_pwrite64);
}
