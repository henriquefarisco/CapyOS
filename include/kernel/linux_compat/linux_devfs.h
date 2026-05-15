#ifndef KERNEL_LINUX_COMPAT_LINUX_DEVFS_H
#define KERNEL_LINUX_COMPAT_LINUX_DEVFS_H

#include <stdint.h>
#include <stddef.h>

/* Minimal pseudo-/dev shim (S2.7).
 *
 * Firefox/SpiderMonkey expect a small set of POSIX device files to
 * be readable/writable as ordinary files. The full S2.7 task in
 * `firefox-port-platform-shim.md` calls for a real devfs mounted at
 * `/dev`; this module is the precursor: a path -> handler table
 * keyed on string match, ready to be wired into the future
 * Linux-ABI `open(2)` adapter.
 *
 * Why a separate module rather than reusing `src/fs/vfs/`:
 *   - The CapyOS VFS is built around capyfs and an inode tree. Adding
 *     pseudo-files requires a full file_ops driver and a mount point.
 *     That is in scope for a later task (S2.x devfs proper).
 *   - For Marco M1 (SpiderMonkey shell) the only real consumer of
 *     /dev paths is NSS/getrandom fallback. A thin string-keyed table
 *     gets us there with ~150 lines of code and no VFS surgery.
 *
 * Devices implemented:
 *   /dev/urandom -> CSPRNG (delegates to `linux_getrandom` semantics)
 *   /dev/random  -> alias of /dev/urandom (same pool)
 *   /dev/null    -> read returns 0 (EOF), write swallows
 *   /dev/zero    -> read fills with 0x00, write swallows
 *   /dev/full    -> read fills with 0x00, write returns -ENOSPC
 *
 * Each device exposes a tiny `read`/`write` operation. There is no
 * stateful "file descriptor" yet: the caller passes the path and the
 * device id is resolved per-call. When S1 grows real `open(2)` the
 * adapter will cache the handler in the FD slot.
 */

enum linux_devfs_id {
    LINUX_DEV_NONE = 0,
    LINUX_DEV_NULL,
    LINUX_DEV_ZERO,
    LINUX_DEV_FULL,
    LINUX_DEV_URANDOM,
    LINUX_DEV_RANDOM,  /* alias of urandom */
};

/* Resolve a path string to a device id. Path is matched verbatim
 * against the canonical names above; relative paths are not
 * supported (Linux semantics for /dev are absolute). Returns
 * `LINUX_DEV_NONE` if the path is not a recognised device. */
enum linux_devfs_id linux_devfs_lookup(const char *path);

/* Read from a device. Returns bytes read, or `-LINUX_E*`.
 * Devices behave like Linux 6.x:
 *   /dev/null               -> always returns 0 (EOF)
 *   /dev/zero, /dev/full    -> always fills `len` bytes with 0x00
 *   /dev/urandom, /dev/random -> CSPRNG via injected source
 * `buf == NULL && len > 0`  -> -LINUX_EFAULT */
int64_t linux_devfs_read(enum linux_devfs_id id, void *buf, size_t len);

/* Write to a device. Returns bytes written, or `-LINUX_E*`.
 *   /dev/null, /dev/zero, /dev/urandom, /dev/random -> always
 *     "consume" len bytes and return len (sink semantics).
 *   /dev/full -> always -LINUX_ENOSPC.
 * `buf == NULL && len > 0` -> -LINUX_EFAULT */
int64_t linux_devfs_write(enum linux_devfs_id id, const void *buf, size_t len);

/* fd encoding: when devfs is reached via the linux_vfs router,
 * each open() call allocates a slot in a small in-module table
 * and returns LINUX_DEVFS_FD_BASE + slot. The id-based API above
 * remains the single source of truth for read/write semantics;
 * the fd API just tracks (fd, id) tuples and dispatches.
 *
 * Range chosen as 0x8000 to live between inotify (0x7000) and
 * shm (0x9000) without collision. */
#define LINUX_DEVFS_FD_BASE 0x8000
#define LINUX_DEVFS_MAX_INSTANCES 16

/* fd-based wrappers used by the VFS router. All take an fd in
 * the range [LINUX_DEVFS_FD_BASE, LINUX_DEVFS_FD_BASE + MAX).
 * Outside that range -> -LINUX_EBADF.
 *
 * `linux_devfs_open` accepts only paths recognised by
 * `linux_devfs_lookup` (see above); other paths -> -LINUX_ENOENT.
 * `flags` is a Linux open(2) bit mask; we honour O_RDONLY/WRONLY/
 * RDWR but ignore O_CREAT/EXCL/TRUNC because /dev nodes are not
 * regular files. Unknown bits in `flags` -> -LINUX_EINVAL.
 *
 * `linux_devfs_lseek_fd` returns 0 unconditionally for character
 * devices (Linux 6.x semantics: SEEK_SET/CUR/END all reset to 0).
 * This matches what /dev/null and /dev/zero do today. */
int64_t linux_devfs_open(const char *path, uint32_t flags);
int64_t linux_devfs_close(int fd);
int64_t linux_devfs_read_fd(int fd, void *buf, size_t len);
int64_t linux_devfs_write_fd(int fd, const void *buf, size_t len);
int64_t linux_devfs_lseek_fd(int fd, int64_t offset, int whence);

/* Test-only reset: required because the urandom path delegates to
 * the linux_random source which itself has a globaling source ptr.
 * Now also clears the fd slot table introduced for the VFS router
 * so tests don't leak state across modules. */
void linux_devfs_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_DEVFS_H */
