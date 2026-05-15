#ifndef KERNEL_LINUX_COMPAT_LINUX_VFS_H
#define KERNEL_LINUX_COMPAT_LINUX_VFS_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI file I/O shim (S5 prep, partial S1).
 *
 * The five primitives userland C runtimes always need:
 *
 *   open(path, flags, mode)             -- get an fd
 *   close(fd)                           -- release fd
 *   read(fd, buf, count)                -- read bytes
 *   write(fd, buf, count)               -- write bytes
 *   lseek(fd, offset, whence)           -- file position
 *
 * Marco M1 strategy:
 *   - Validate flag/parameter shapes
 *   - Route via injected ops to the underlying primitive:
 *       /dev/urandom etc.    -> linux_devfs
 *       /dev/shm/<name>      -> linux_shm (when wiring lands)
 *       capyfs paths         -> capyfs (when fs ops landam)
 *       pipe/eventfd/epoll   -> their own modules (recognised by
 *                                fd-range)
 *   - Return -ENOSYS for unsupported paths until the routing
 *     layer fills in
 *
 * This module is the "front door" for Linux file syscalls. All
 * future expansions (openat, fstat, readv, etc.) wrap into it.
 */

/* Linux open() flags subset. The `O_*` constants on x86_64 Linux. */
#define LINUX_VFS_O_RDONLY    0x0u
#define LINUX_VFS_O_WRONLY    0x1u
#define LINUX_VFS_O_RDWR      0x2u
#define LINUX_VFS_O_ACCMODE   0x3u
#define LINUX_VFS_O_CREAT     0x40u
#define LINUX_VFS_O_EXCL      0x80u
#define LINUX_VFS_O_NOCTTY    0x100u
#define LINUX_VFS_O_TRUNC     0x200u
#define LINUX_VFS_O_APPEND    0x400u
#define LINUX_VFS_O_NONBLOCK  0x800u
#define LINUX_VFS_O_DIRECTORY 0x10000u
#define LINUX_VFS_O_NOFOLLOW  0x20000u
#define LINUX_VFS_O_CLOEXEC   0x80000u
#define LINUX_VFS_O_PATH      0x200000u

#define LINUX_VFS_OPEN_KNOWN_FLAGS \
    (LINUX_VFS_O_ACCMODE | LINUX_VFS_O_CREAT | LINUX_VFS_O_EXCL | \
     LINUX_VFS_O_NOCTTY | LINUX_VFS_O_TRUNC | LINUX_VFS_O_APPEND | \
     LINUX_VFS_O_NONBLOCK | LINUX_VFS_O_DIRECTORY | \
     LINUX_VFS_O_NOFOLLOW | LINUX_VFS_O_CLOEXEC | LINUX_VFS_O_PATH)

/* lseek whence. */
#define LINUX_SEEK_SET 0
#define LINUX_SEEK_CUR 1
#define LINUX_SEEK_END 2
#define LINUX_SEEK_DATA 3
#define LINUX_SEEK_HOLE 4

#define LINUX_VFS_PATH_MAX 4096u

/* Routing callback bundle.
 *
 * Contract: every callback returns the success value (fd or
 * byte count) on success and `-LINUX_E*` on failure. The shim
 * passes the errno through to userland verbatim; only a bare
 * `-1` (legacy default) is rewritten to `-EBADF`/`-ENOENT`. */
struct linux_vfs_ops {
    /* Resolve path -> kernel-visible fd. The implementation is
     * responsible for matching prefixes (e.g. "/dev/",
     * "/dev/shm/", "/tmp/") and returning the appropriate errno
     * for paths it does not own (typically -LINUX_ENOENT). */
    int  (*open) (const char *path, uint32_t flags, uint32_t mode);
    int  (*close)(int fd);
    int64_t (*read) (int fd, void *buf, size_t len);
    int64_t (*write)(int fd, const void *buf, size_t len);
    int64_t (*lseek)(int fd, int64_t offset, int whence);
};

void linux_vfs_install_ops(const struct linux_vfs_ops *ops);
void linux_vfs_reset_for_tests(void);

int64_t linux_vfs_open(uint64_t path_ptr, uint32_t flags, uint32_t mode);
int64_t linux_vfs_close(int fd);
int64_t linux_vfs_read(int fd, uint64_t buf_ptr, size_t len);
int64_t linux_vfs_write(int fd, uint64_t buf_ptr, size_t len);
int64_t linux_vfs_lseek(int fd, int64_t offset, int whence);

/* Linux AT_FDCWD: special dirfd value that says "interpret path as
 * relative to current working directory". musl uses this for the
 * `open()` libc call (which dispatches to `openat` under the hood
 * on modern kernels). Defined in `<fcntl.h>`. */
#define LINUX_AT_FDCWD (-100)

/* `openat(dirfd, path, flags, mode)` -- the modern alternative to
 * `open(2)`. Linux ABI semantics:
 *
 *   - `dirfd == AT_FDCWD`: path is treated as absolute or relative
 *     to the current working directory (we don't track cwd yet so
 *     we accept absolute only).
 *   - `dirfd >= 0`: path is relative to the directory referenced
 *     by `dirfd`. CapyOS doesn't expose directory fds; for Marco M1
 *     we return -ENOTDIR so musl falls back to absolute paths.
 *
 * Return: same as `linux_vfs_open` (fd >= 0 or -errno). */
int64_t linux_vfs_openat(int dirfd, uint64_t path_ptr,
                         uint32_t flags, uint32_t mode);

void linux_vfs_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_VFS_H */
