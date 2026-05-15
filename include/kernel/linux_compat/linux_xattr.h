#ifndef KERNEL_LINUX_COMPAT_LINUX_XATTR_H
#define KERNEL_LINUX_COMPAT_LINUX_XATTR_H

/* Linux ABI extended-attribute syscalls.
 *
 *   ssize_t getxattr   (const char *path, const char *name,
 *                       void *value, size_t size);
 *   ssize_t lgetxattr  (...) -- like getxattr but doesn't follow symlinks
 *   ssize_t fgetxattr  (int fd, const char *name, void *value, size_t size);
 *   int     setxattr   (const char *path, const char *name,
 *                       const void *value, size_t size, int flags);
 *   int     lsetxattr  (...) -- like setxattr but doesn't follow symlinks
 *   int     fsetxattr  (int fd, ...);
 *   ssize_t listxattr  (const char *path, char *list, size_t size);
 *   ssize_t llistxattr (...) -- l-form
 *   ssize_t flistxattr (int fd, char *list, size_t size);
 *   int     removexattr (const char *path, const char *name);
 *   int     lremovexattr(...) -- l-form
 *   int     fremovexattr(int fd, const char *name);
 *
 * Why this matters for the Firefox port:
 *   - Firefox quarantine logic (downloaded files) uses
 *     setxattr("user.com.apple.quarantine", ...) on macOS but
 *     `user.xdg.origin.url` etc. on Linux. -ENOSYS aborts the
 *     "where did this come from" tracking.
 *   - SELinux-aware code paths in Mesa, dbus, etc. probe via
 *     getxattr/listxattr; the Linux convention is that filesystems
 *     that don't support xattrs return -ENOTSUP, which userland
 *     handles gracefully.
 *   - musl `cp -a`-style code uses listxattr to discover and copy
 *     attribute sets when preserving file metadata.
 *
 * Marco M1 has no xattr storage today (tmpfs doesn't carry one).
 * The Linux convention for "filesystem doesn't support xattrs at
 * all" is to return -ENOTSUP from setxattr/removexattr and to
 * report 0 attributes from listxattr/getxattr (or -ENODATA on
 * getxattr for a missing attribute). We implement that semantic
 * faithfully; userland code that probes for xattr support takes
 * the "no support" branch deterministically.
 *
 * setxattr flags constants:
 *   - XATTR_CREATE  -> "the attribute must not exist"
 *   - XATTR_REPLACE -> "the attribute must already exist"
 * We accept the flag mask whitelist and reject other bits with
 * -EINVAL so userland sees the right errno path. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_XATTR_CREATE           0x1
#define LINUX_XATTR_REPLACE          0x2
#define LINUX_XATTR_KNOWN_FLAGS      (LINUX_XATTR_CREATE | LINUX_XATTR_REPLACE)

/* Linux limits (from include/uapi/linux/limits.h). */
#define LINUX_XATTR_NAME_MAX         255
#define LINUX_XATTR_SIZE_MAX         65536
#define LINUX_XATTR_LIST_MAX         65536

int64_t linux_setxattr   (const char *path, const char *name,
                          const void *value, size_t size, int flags);
int64_t linux_lsetxattr  (const char *path, const char *name,
                          const void *value, size_t size, int flags);
int64_t linux_fsetxattr  (int fd, const char *name,
                          const void *value, size_t size, int flags);

int64_t linux_getxattr   (const char *path, const char *name,
                          void *value, size_t size);
int64_t linux_lgetxattr  (const char *path, const char *name,
                          void *value, size_t size);
int64_t linux_fgetxattr  (int fd, const char *name,
                          void *value, size_t size);

int64_t linux_listxattr  (const char *path, char *list, size_t size);
int64_t linux_llistxattr (const char *path, char *list, size_t size);
int64_t linux_flistxattr (int fd, char *list, size_t size);

int64_t linux_removexattr (const char *path, const char *name);
int64_t linux_lremovexattr(const char *path, const char *name);
int64_t linux_fremovexattr(int fd, const char *name);

void linux_xattr_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_XATTR_H */
