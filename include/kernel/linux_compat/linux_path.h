#ifndef KERNEL_LINUX_COMPAT_LINUX_PATH_H
#define KERNEL_LINUX_COMPAT_LINUX_PATH_H

/* Linux ABI path-related queries: `getcwd`, `readlink`, `readlinkat`.
 *
 * Marco M1 strategy:
 *
 *   - getcwd: report "/" as the universal cwd. CapyOS doesn't
 *     track per-process cwd yet (chdir is also unwired), so all
 *     paths userland passes are absolute or relative-to-root.
 *     musl's path-resolution helpers tolerate a fixed cwd of "/"
 *     because they always rewrite relative paths to absolute
 *     before issuing syscalls.
 *
 *   - readlink: pretend that almost nothing is a symlink. The one
 *     special case is `/proc/self/exe` -- userland code (musl
 *     itself, Firefox crash reporter, etc.) reads this to find
 *     the binary on disk. We forward to a callback that
 *     userland-name-aware code (eg the procfs renderer) installs.
 *     Other paths -> -EINVAL (Linux: "not a symlink"). NULL path
 *     -> -EFAULT.
 *
 *   - readlinkat: dirfd-prefixed variant; AT_FDCWD path delegates
 *     directly to readlink. Other dirfd values -> -ENOTDIR (we
 *     don't expose directory fds). */

#include <stdint.h>
#include <stddef.h>

#define LINUX_PATH_AT_FDCWD (-100)

/* Provider that maps the `/proc/self/exe` magic path to a real
 * filesystem path. Returns the byte count written (no NUL) on
 * success, -errno on failure. */
struct linux_path_providers {
    int64_t (*resolve_proc_self_exe)(char *buf, size_t bufsize);
};

void linux_path_install(const struct linux_path_providers *p);
void linux_path_reset_for_tests(void);

/* Linux semantics:
 *   - buf == NULL -> -EFAULT
 *   - size == 0 -> -EINVAL (Linux's documented behaviour)
 *   - buf "/" plus NUL = 2 bytes; size < 2 -> -ERANGE
 *   - returns the byte count INCLUDING the NUL terminator
 *     (musl follows this; glibc differs but tolerates) */
int64_t linux_getcwd(char *buf, size_t size);

/* Linux semantics:
 *   - path == NULL or buf == NULL -> -EFAULT
 *   - bufsize == 0 -> -EINVAL
 *   - path == "/proc/self/exe" -> calls provider; up to bufsize
 *     bytes copied (no NUL). Returns byte count.
 *   - other paths -> -EINVAL ("not a symlink") -- Linux behaviour
 *     for non-symlink targets. */
int64_t linux_readlink(const char *path, char *buf, size_t bufsize);

/* `readlinkat(dirfd, path, buf, bufsize)`. If `dirfd ==
 * AT_FDCWD`, delegates to `linux_readlink`. Other dirfd values
 * are not supported (CapyOS doesn't expose directory fds yet)
 * so we return -ENOTDIR. */
int64_t linux_readlinkat(int dirfd, const char *path, char *buf, size_t bufsize);

void linux_path_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PATH_H */
