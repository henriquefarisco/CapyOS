#ifndef KERNEL_LINUX_COMPAT_LINUX_IOCTL_H
#define KERNEL_LINUX_COMPAT_LINUX_IOCTL_H

/* Linux ABI `ioctl(2)` -- the catch-all device control syscall.
 *
 * Linux defines hundreds of ioctl commands (terminal control via
 * TCGETS/TCSETS, socket control via SIOCGIFADDR, framebuffer
 * control via FBIOGET_VSCREENINFO, etc.) and userland code
 * issues them constantly. CapyOS doesn't implement any of these
 * device classes yet; what we DO need is the *correct Linux
 * error semantics* so that musl/glibc/SpiderMonkey detect "not
 * a tty" and proceed to fully-buffered stdio mode rather than
 * crashing on an unexpected -ENOSYS.
 *
 * Specifically musl's stdio init runs:
 *
 *     ioctl(fd, TCGETS, &termios)   // is fd a terminal?
 *
 * Linux returns -ENOTTY for any fd that is not a terminal,
 * including all regular files, pipes, sockets, and our pseudo-fs
 * fds. Returning -ENOTTY makes musl set the stream to
 * fully-buffered mode (block-buffered for files, unbuffered for
 * stderr). This is exactly what we want at Marco M1.
 *
 * Failure modes:
 *   -LINUX_EBADF    fd < 0
 *
 * Otherwise we return -LINUX_ENOTTY for ALL ioctl commands.
 *
 * Future: when CapyOS grows real terminals or sockets, replace
 * this stub with a per-fd dispatch keyed on the fd encoding
 * range (devfs/pipe/socket). The errno semantics above stay
 * the same -- only the fd table grows. */

#include <stdint.h>

/* Common Linux ioctl command codes, exposed so tests can
 * verify the exact musl init pattern (TCGETS -> -ENOTTY). */
#define LINUX_TCGETS      0x5401  /* termios get */
#define LINUX_TCSETS      0x5402  /* termios set */
#define LINUX_TCSETSW     0x5403
#define LINUX_TCSETSF     0x5404
#define LINUX_TIOCGWINSZ  0x5413  /* terminal window size */
#define LINUX_FIONREAD    0x541B  /* bytes available */
#define LINUX_FIOCLEX     0x5451  /* set FD_CLOEXEC */
#define LINUX_FIONCLEX    0x5450  /* clear FD_CLOEXEC */
#define LINUX_FIONBIO     0x5421  /* set/clear non-blocking */

/* Returns 0 on success or negative Linux errno on failure.
 * Marco M1 implementation always reports -ENOTTY for valid
 * fds and -EBADF for negative fds. */
int64_t linux_ioctl(int fd, uint32_t cmd, uint64_t arg);

void linux_ioctl_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_IOCTL_H */
