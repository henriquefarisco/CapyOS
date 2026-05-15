#include "kernel/linux_compat/linux_fcntl.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

#define FD_TABLE_BUCKETS 256

/* Per-fd flag storage. fd values can be large (encoded with high
 * bits indicating backend, e.g. 0x8000 devfs, 0x8800 procfs).
 * To keep memory bounded we hash fd to a 256-bucket table; on
 * collision the latest setter wins (acceptable for Marco M1
 * since each test scenario uses a small set of fds and musl
 * stdio only touches fds 0/1/2). When the proper fd table lands,
 * this storage migrates there. */
static uint8_t  g_fd_flags[FD_TABLE_BUCKETS];   /* FD_CLOEXEC */
static uint32_t g_file_flags[FD_TABLE_BUCKETS];  /* O_APPEND etc */

/* Default access mode for every fd. F_GETFL must include the
 * access mode in its return value; we report O_RDWR as a
 * permissive default until per-fd open-flag tracking lands. */
#define LINUX_FCNTL_O_RDWR_DEFAULT 0x0002

static size_t bucket_of(int fd) {
    return (size_t)((unsigned int)fd) % FD_TABLE_BUCKETS;
}

void linux_fcntl_reset_for_tests(void) {
    for (size_t i = 0; i < FD_TABLE_BUCKETS; i++) {
        g_fd_flags[i] = 0;
        g_file_flags[i] = 0;
    }
}

int64_t linux_fcntl(int fd, uint32_t cmd, uint64_t arg) {
    if (fd < 0) return -LINUX_EBADF;
    size_t b = bucket_of(fd);

    switch (cmd) {
      case LINUX_F_GETFD:
        return (int64_t)g_fd_flags[b];

      case LINUX_F_SETFD:
        /* Only FD_CLOEXEC is meaningful; mask off everything else. */
        g_fd_flags[b] = (uint8_t)(arg & LINUX_FD_CLOEXEC);
        return 0;

      case LINUX_F_GETFL:
        /* Linux returns access mode | the writable file flags
         * currently set. Access mode defaulted to O_RDWR. */
        return (int64_t)(LINUX_FCNTL_O_RDWR_DEFAULT |
                         g_file_flags[b]);

      case LINUX_F_SETFL:
        /* Linux only honours the SETFL-allowed subset; other
         * bits (access mode, O_CREAT/EXCL/TRUNC) are silently
         * ignored. We honour the same mask. */
        g_file_flags[b] = (uint32_t)(arg & LINUX_FCNTL_SETFL_MASK);
        return 0;

      case LINUX_F_DUPFD:
      case LINUX_F_DUPFD_CLOEXEC:
        /* Need a real fd table that can clone the underlying
         * object's reference. S1.13 follow-up. */
        return -LINUX_ENOSYS;

      case LINUX_F_GETLK:
      case LINUX_F_SETLK:
      case LINUX_F_SETLKW:
        /* POSIX advisory locks. musl stdio doesn't use these.
         * Returning -ENOSYS lets userland code degrade
         * gracefully. */
        return -LINUX_ENOSYS;

      default:
        return -LINUX_EINVAL;
    }
}

static int64_t sys_fcntl(const struct linux_syscall_args *a) {
    return linux_fcntl((int)a->a0, (uint32_t)a->a1, a->a2);
}

void linux_fcntl_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_fcntl, sys_fcntl);
}
