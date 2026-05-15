#ifndef KERNEL_LINUX_COMPAT_LINUX_TRUNC_H
#define KERNEL_LINUX_COMPAT_LINUX_TRUNC_H

/* Linux ABI `truncate(2)` and `ftruncate(2)` -- resize a file.
 *
 *   int truncate(const char *path, off_t length);
 *   int ftruncate(int fd, off_t length);
 *
 * `truncate` operates on a path; until the namei walker exists
 * we return -ENOSYS for any non-NULL path (-EFAULT for NULL,
 * -EINVAL for negative length).
 *
 * `ftruncate` operates on an fd. CapyOS's fd-encoding scheme
 * routes to per-backend handlers (devfs/procfs/shm/tmpfs) via
 * a provider table. tmpfs supports resize natively; other
 * backends (procfs read-only, devfs char-only) reject with
 * -EINVAL. The boot init installs the dispatch table; absent
 * an installer the call returns -ENOSYS.
 *
 * Common Linux semantics applied here:
 *   - length < 0 -> -EINVAL
 *   - fd     < 0 -> -EBADF
 *   - path == NULL -> -EFAULT (not -EINVAL)
 *   - path == ""   -> -ENOENT
 *
 * Userland that calls `ftruncate` on a writable shm/tmpfs fd
 * eventually gets the real resize once the provider lands.
 * Userland calling on an unsupported fd (procfs) gets -EINVAL,
 * which is the documented Linux answer for non-resizable fds. */

#include <stdint.h>

struct linux_trunc_ops {
    /* Return 0 on success, -errno on failure. Caller has
     * already validated fd >= 0 and length >= 0. */
    int64_t (*ftruncate)(int fd, int64_t length);
};

void linux_trunc_install_ops(const struct linux_trunc_ops *ops);
void linux_trunc_reset_for_tests(void);

int64_t linux_truncate(const char *path, int64_t length);
int64_t linux_ftruncate(int fd, int64_t length);

void linux_trunc_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_TRUNC_H */
