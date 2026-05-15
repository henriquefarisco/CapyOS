#ifndef KERNEL_LINUX_COMPAT_LINUX_INOTIFY_H
#define KERNEL_LINUX_COMPAT_LINUX_INOTIFY_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI inotify shim (S1.16).
 *
 * Marco M1 surface:
 *
 *   inotify_init1(flags)              -- create inotify fd
 *   inotify_add_watch(fd, path, mask) -- add watch, returns wd
 *   inotify_rm_watch(fd, wd)          -- remove watch
 *
 * Why this matters for Firefox:
 *   - xpcom prefs watcher uses inotify on Linux to detect
 *     prefs.js changes.
 *   - Some font-config code paths invoke inotify_init at
 *     startup; failing IN1 -> -ENOSYS aborts.
 *
 * Marco M1 status: the watch list is stored, but events are
 * never generated (CapyOS has no fs change notifier yet).
 * Userland sees an empty event stream from the inotify fd,
 * which xpcom tolerates (it polls anyway as a fallback).
 */

/* inotify_init1 flags. */
#define LINUX_IN_CLOEXEC  0x080000u
#define LINUX_IN_NONBLOCK 0x000800u
#define LINUX_IN_INIT_KNOWN_FLAGS (LINUX_IN_CLOEXEC | LINUX_IN_NONBLOCK)

/* inotify event mask bits we recognise (uapi/linux/inotify.h). */
#define LINUX_IN_ACCESS        0x00000001u
#define LINUX_IN_MODIFY        0x00000002u
#define LINUX_IN_ATTRIB        0x00000004u
#define LINUX_IN_CLOSE_WRITE   0x00000008u
#define LINUX_IN_CLOSE_NOWRITE 0x00000010u
#define LINUX_IN_OPEN          0x00000020u
#define LINUX_IN_MOVED_FROM    0x00000040u
#define LINUX_IN_MOVED_TO      0x00000080u
#define LINUX_IN_CREATE        0x00000100u
#define LINUX_IN_DELETE        0x00000200u
#define LINUX_IN_DELETE_SELF   0x00000400u
#define LINUX_IN_MOVE_SELF     0x00000800u
#define LINUX_IN_ALL_EVENTS    0x00000FFFu

#define LINUX_IN_ONLYDIR     0x01000000u
#define LINUX_IN_DONT_FOLLOW 0x02000000u
#define LINUX_IN_EXCL_UNLINK 0x04000000u
#define LINUX_IN_MASK_CREATE 0x10000000u
#define LINUX_IN_MASK_ADD    0x20000000u
#define LINUX_IN_ISDIR       0x40000000u
#define LINUX_IN_ONESHOT     0x80000000u

#define LINUX_IN_KNOWN_MASK \
    (LINUX_IN_ALL_EVENTS | LINUX_IN_ONLYDIR | LINUX_IN_DONT_FOLLOW | \
     LINUX_IN_EXCL_UNLINK | LINUX_IN_MASK_CREATE | LINUX_IN_MASK_ADD | \
     LINUX_IN_ISDIR | LINUX_IN_ONESHOT)

/* fd encoding. */
#define LINUX_INOTIFY_FD_BASE 0x7000
#define LINUX_INOTIFY_MAX_INSTANCES 8
#define LINUX_INOTIFY_MAX_PER_INSTANCE 32

void linux_inotify_reset_for_tests(void);

int64_t linux_inotify_init1(uint32_t flags);
int64_t linux_inotify_add_watch(int fd, uint64_t path_ptr, uint32_t mask);
int64_t linux_inotify_rm_watch(int fd, int wd);
int64_t linux_inotify_close(int fd);
int64_t linux_inotify_read(int fd, void *buf, size_t len);
int64_t linux_inotify_write(int fd, const void *buf, size_t len);
int64_t linux_inotify_lseek(int fd, int64_t offset, int whence);

void linux_inotify_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_INOTIFY_H */
