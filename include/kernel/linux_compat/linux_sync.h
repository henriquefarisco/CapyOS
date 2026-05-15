#ifndef KERNEL_LINUX_COMPAT_LINUX_SYNC_H
#define KERNEL_LINUX_COMPAT_LINUX_SYNC_H

/* Linux ABI durability barrier syscalls.
 *
 *   void sync(void);
 *   int  syncfs(int fd);
 *   int  fsync(int fd);
 *   int  fdatasync(int fd);
 *
 * Why this matters for the Firefox port:
 *   - Firefox cache writes follow a "open + write + fsync + rename"
 *     pattern for crash-safe atomic updates. fsync -ENOSYS makes
 *     the cache silently revert to the non-atomic path -- corrupted
 *     cache after every browser crash.
 *   - SQLite (used by places.sqlite, cookies.sqlite, etc.) calls
 *     fsync after every WAL checkpoint. -ENOSYS triggers SQLite's
 *     "disk I/O error" path which corrupts the database.
 *   - musl `sync()` is also called by `umount(2)` and by graceful
 *     shutdown helpers.
 *
 * Marco M1 has no persistent backing store yet (tmpfs is purely
 * RAM-resident), so durability is trivially satisfied: every write
 * is already visible. We accept all four syscalls with full Linux
 * validation:
 *   - fd-taking forms (syncfs/fsync/fdatasync) require fd >= 0.
 *   - sync() returns void in libc but the syscall returns 0; we
 *     return 0.
 * Provider injection lets a future on-disk fs flush real device
 * caches when persistence lands. */

#include <stdint.h>

struct linux_sync_ops {
    /* Returns 0 on success, negative errno on failure. NULL =
     * caller falls back to "no persistence" success (return 0). */
    int64_t (*sync_all)(void);
    int64_t (*sync_fs) (int fd);
    int64_t (*sync_fd) (int fd, int data_only);
};

void linux_sync_install_ops(const struct linux_sync_ops *ops);
void linux_sync_reset_for_tests(void);

int64_t linux_sync     (void);
int64_t linux_syncfs   (int fd);
int64_t linux_fsync    (int fd);
int64_t linux_fdatasync(int fd);

void linux_sync_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SYNC_H */
