#ifndef KERNEL_LINUX_COMPAT_LINUX_FD_H
#define KERNEL_LINUX_COMPAT_LINUX_FD_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI fd-flags shims (S1.13).
 *
 * `pipe(int *fds)` is the legacy zero-flags form. `pipe2(int *fds,
 * int flags)` and `dup3(oldfd, newfd, flags)` differ from the
 * legacy `pipe`/`dup2` only by accepting flag bits (`O_CLOEXEC`,
 * `O_NONBLOCK`, `O_DIRECT`). The kernel pipe primitive stays the
 * same; the shim:
 *
 *   1. Validates that `flags` contains only known bits.
 *   2. Calls into an injected `pipe_create_fn` / `dup_fn` to
 *      get the underlying fd pair / duplicated fd.
 *   3. Stores per-fd flag state via an injected setter.
 *
 * Layering identical to clock/random/process: pure logic +
 * callbacks injected at boot.
 *
 * The flag bits below mirror Linux uapi:
 *   O_CLOEXEC  = 02000000 (octal) = 0x080000
 *   O_NONBLOCK = 00004000 (octal) = 0x000800
 *   O_DIRECT   = 00040000 (octal) = 0x004000
 *
 * dup3 accepts only `O_CLOEXEC` (Linux man page). Passing any
 * other flag returns -EINVAL.
 */

#define LINUX_O_NONBLOCK 0x000800u   /* 04000 octal  */
#define LINUX_O_DIRECT   0x004000u   /* 040000 octal */
#define LINUX_O_CLOEXEC  0x080000u   /* 02000000 octal */

#define LINUX_PIPE2_KNOWN_FLAGS \
    (LINUX_O_NONBLOCK | LINUX_O_DIRECT | LINUX_O_CLOEXEC)

/* dup3 only takes O_CLOEXEC and is the only difference from
 * dup2 (which clears O_CLOEXEC implicitly). */
#define LINUX_DUP3_KNOWN_FLAGS LINUX_O_CLOEXEC

/* Callback bundle. */
struct linux_fd_ops {
    /* Create a pipe. Fills `fds[2]` with read-fd / write-fd
     * (Linux ordering). Returns 0 on success or -1 on failure
     * (caller maps to -EMFILE). */
    int (*pipe_create)(int fds_out[2]);
    /* Duplicate `oldfd` into the kernel fd table. Returns the
     * new fd or -1 on failure. dup3 specifies `newfd` exactly:
     * the helper must close any existing fd at that slot first
     * and use the requested number. Returns -1 on failure. */
    int (*dup3)(int oldfd, int newfd);
    /* Apply the flag mask onto `fd`. CapyOS does not yet have a
     * full fd_flags table; this is a hook so the implementation
     * can grow without changing the shim. NULL is permitted --
     * shim treats flags as advisory. */
    void (*set_fd_flags)(int fd, uint32_t flags);
};

void linux_fd_install_ops(const struct linux_fd_ops *ops);
void linux_fd_reset_for_tests(void);

/* Core entry points. */

/* `pipe(int *fds_out)`.
 *   fds_out == NULL                        -> -LINUX_EFAULT
 *   no ops installed                       -> -LINUX_ENOSYS
 *   pipe_create returns -1                 -> -LINUX_EMFILE
 *   success: identical to pipe2(fds_out, 0).
 */
int64_t linux_pipe(int *fds_out);

/* `pipe2(int *fds_out, uint32_t flags)`.
 *   fds_out == NULL                        -> -LINUX_EFAULT
 *   flags has unknown bits                 -> -LINUX_EINVAL
 *   no ops installed                       -> -LINUX_ENOSYS
 *   pipe_create returns -1                 -> -LINUX_EMFILE
 *   success: fds_out[0..1] populated, ops->set_fd_flags called
 *   for each fd. Returns 0.
 */
int64_t linux_pipe2(int *fds_out, uint32_t flags);

/* `dup3(int oldfd, int newfd, uint32_t flags)`.
 *   oldfd == newfd                         -> -LINUX_EINVAL (Linux semantics)
 *   flags has bits outside LINUX_O_CLOEXEC -> -LINUX_EINVAL
 *   no ops installed                       -> -LINUX_ENOSYS
 *   dup3 returns -1                        -> -LINUX_EBADF
 *   success: returns the new fd (== newfd) and applies flags.
 */
int64_t linux_dup3(int oldfd, int newfd, uint32_t flags);

void linux_fd_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FD_H */
