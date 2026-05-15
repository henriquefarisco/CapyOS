#ifndef KERNEL_LINUX_COMPAT_LINUX_FCNTL_H
#define KERNEL_LINUX_COMPAT_LINUX_FCNTL_H

/* Linux ABI `fcntl(2)` -- file descriptor manipulation.
 *
 * fcntl is a multiplexer like ioctl, but with a small set of
 * commands. The ones musl/glibc actually issue during stdio
 * init and process startup are:
 *
 *   F_GETFD          -> read FD_CLOEXEC flag
 *   F_SETFD          -> write FD_CLOEXEC flag
 *   F_GETFL          -> read O_RDONLY/O_WRONLY/O_RDWR + O_NONBLOCK + O_APPEND
 *   F_SETFL          -> write the writable subset of file flags
 *   F_DUPFD          -> dup fd to lowest >= arg
 *   F_DUPFD_CLOEXEC  -> dup fd to lowest >= arg, with CLOEXEC
 *
 * Marco M1 storage: this module owns a flat table of 256 entries
 * keyed by fd value (cap chosen to comfortably cover every fd
 * we currently encode -- devfs 0x8000, procfs 0x8800, shm 0x9000,
 * tmpfs 0xA000, et al; the table is sparse and indexes by a
 * small fd_slot computed via mod-256 hashing of the encoded fd
 * to keep memory bounded). Each entry stores fd_flags
 * (FD_CLOEXEC) and a snapshot of file_flags (O_NONBLOCK / O_APPEND).
 *
 * Limitations:
 *   - F_GETFL access mode (O_RDWR/O_RDONLY/O_WRONLY) is fixed at
 *     O_RDWR until the per-fd open()-flags stream is wired.
 *     musl/glibc tolerate: they only check `flags & O_ACCMODE`
 *     to know which I/O direction is permitted, and we already
 *     allow both via the underlying VFS callbacks.
 *   - F_DUPFD / F_DUPFD_CLOEXEC return -ENOSYS today; they
 *     require a real fd table that knows how to clone an
 *     existing fd's underlying object. Wired in S1.13 follow-up.
 *   - Locking commands (F_GETLK/F_SETLK/F_SETLKW) return -ENOSYS.
 *     musl's stdio doesn't use them; userland code that needs
 *     POSIX locks degrades gracefully.
 *
 * When the proper fd table (S1.13 dup3-functional follow-up)
 * lands, the per-fd flags storage migrates here. */

#include <stdint.h>
#include <stddef.h>

/* Linux fcntl command codes (from <fcntl.h>). */
#define LINUX_F_DUPFD          0
#define LINUX_F_GETFD          1
#define LINUX_F_SETFD          2
#define LINUX_F_GETFL          3
#define LINUX_F_SETFL          4
#define LINUX_F_GETLK          5
#define LINUX_F_SETLK          6
#define LINUX_F_SETLKW         7
#define LINUX_F_DUPFD_CLOEXEC  1030

/* Linux fd_flags (returned by F_GETFD, set by F_SETFD). Only
 * one bit defined: FD_CLOEXEC = 1. */
#define LINUX_FD_CLOEXEC       1

/* Linux file flags subset that F_SETFL is allowed to change.
 * From Linux: only O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME,
 * O_NONBLOCK can be set via F_SETFL (the access mode and
 * O_CREAT/O_EXCL/O_TRUNC are immutable post-open). */
#define LINUX_FCNTL_O_APPEND     0x0400
#define LINUX_FCNTL_O_NONBLOCK   0x0800
#define LINUX_FCNTL_O_DIRECT     0x4000
#define LINUX_FCNTL_O_NOATIME    0x40000
#define LINUX_FCNTL_O_ASYNC      0x2000
#define LINUX_FCNTL_SETFL_MASK \
    (LINUX_FCNTL_O_APPEND | LINUX_FCNTL_O_NONBLOCK | \
     LINUX_FCNTL_O_DIRECT | LINUX_FCNTL_O_NOATIME | \
     LINUX_FCNTL_O_ASYNC)

/* Reset module-local state (fd_flags + file_flags tables).
 * Tests call between scenarios; production never. */
void linux_fcntl_reset_for_tests(void);

/* `fcntl(fd, cmd, arg)` Linux semantics for the supported subset.
 *
 * Returns:
 *   F_GETFD  -> 0 or FD_CLOEXEC
 *   F_SETFD  -> 0 (always succeeds for any flag value;
 *                  unknown bits silently dropped)
 *   F_GETFL  -> O_RDWR | (current O_APPEND | O_NONBLOCK | ...)
 *   F_SETFL  -> 0 (only the SETFL_MASK bits are honoured)
 *   F_DUPFD / F_DUPFD_CLOEXEC -> -ENOSYS
 *   F_GETLK / F_SETLK / F_SETLKW -> -ENOSYS
 *   default  -> -EINVAL
 *
 * fd < 0 -> -EBADF. */
int64_t linux_fcntl(int fd, uint32_t cmd, uint64_t arg);

void linux_fcntl_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FCNTL_H */
