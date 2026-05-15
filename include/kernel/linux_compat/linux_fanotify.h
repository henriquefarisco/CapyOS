#ifndef KERNEL_LINUX_COMPAT_LINUX_FANOTIFY_H
#define KERNEL_LINUX_COMPAT_LINUX_FANOTIFY_H

/* Linux ABI fanotify syscalls.
 *
 *   int fanotify_init(unsigned int flags, unsigned int event_f_flags);
 *   int fanotify_mark(int fanotify_fd, unsigned int flags,
 *                     uint64_t mask, int dirfd, const char *pathname);
 *
 * Why this matters for the Firefox port:
 *   - Snap-packaged Firefox uses fanotify to monitor cache and
 *     profile directories for external changes; -ENOSYS makes
 *     the snap layer fall back to inotify (which we already
 *     have) but emits a warning.
 *   - Some sandbox monitoring tools (auditd, file-integrity
 *     monitors) install fanotify_mark to log file accesses;
 *     -ENOSYS disables them gracefully.
 *
 * Linux semantics:
 *   - fanotify_init flags: CLOEXEC, NONBLOCK, CLASS_NOTIF/
 *     CLASS_CONTENT/CLASS_PRE_CONTENT, REPORT_TID, REPORT_FID,
 *     REPORT_DIR_FID, REPORT_NAME.
 *   - event_f_flags must be valid file open flag mask.
 *   - fanotify_mark flags: ADD/REMOVE/FLUSH | MOUNT/INODE/
 *     FILESYSTEM | DONT_FOLLOW/ONLYDIR | EVICTABLE.
 *
 * Marco M1 has no fanotify infrastructure. We allocate fanotify
 * fds from a small table so userland's "did the kernel support
 * what I asked for?" probe takes its happy path. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_FAN_FD_BASE   0xC000
#define LINUX_FAN_FD_MAX    8

/* fanotify_init flags. */
#define LINUX_FAN_CLOEXEC          0x00000001
#define LINUX_FAN_NONBLOCK         0x00000002
#define LINUX_FAN_CLASS_NOTIF      0x00000000
#define LINUX_FAN_CLASS_CONTENT    0x00000004
#define LINUX_FAN_CLASS_PRE_CONTENT 0x00000008
#define LINUX_FAN_UNLIMITED_QUEUE  0x00000010
#define LINUX_FAN_UNLIMITED_MARKS  0x00000020
#define LINUX_FAN_ENABLE_AUDIT     0x00000040
#define LINUX_FAN_REPORT_PIDFD     0x00000080
#define LINUX_FAN_REPORT_TID       0x00000100
#define LINUX_FAN_REPORT_FID       0x00000200
#define LINUX_FAN_REPORT_DIR_FID   0x00000400
#define LINUX_FAN_REPORT_NAME      0x00000800
#define LINUX_FAN_REPORT_TARGET_FID 0x00001000

#define LINUX_FAN_INIT_KNOWN \
    (LINUX_FAN_CLOEXEC | LINUX_FAN_NONBLOCK | \
     LINUX_FAN_CLASS_CONTENT | LINUX_FAN_CLASS_PRE_CONTENT | \
     LINUX_FAN_UNLIMITED_QUEUE | LINUX_FAN_UNLIMITED_MARKS | \
     LINUX_FAN_ENABLE_AUDIT | LINUX_FAN_REPORT_PIDFD | \
     LINUX_FAN_REPORT_TID | LINUX_FAN_REPORT_FID | \
     LINUX_FAN_REPORT_DIR_FID | LINUX_FAN_REPORT_NAME | \
     LINUX_FAN_REPORT_TARGET_FID)

/* fanotify_mark flags. */
#define LINUX_FAN_MARK_ADD         0x00000001
#define LINUX_FAN_MARK_REMOVE      0x00000002
#define LINUX_FAN_MARK_DONT_FOLLOW 0x00000004
#define LINUX_FAN_MARK_ONLYDIR     0x00000008
#define LINUX_FAN_MARK_MOUNT       0x00000010
#define LINUX_FAN_MARK_IGNORED_MASK 0x00000020
#define LINUX_FAN_MARK_IGNORED_SURV_MODIFY 0x00000040
#define LINUX_FAN_MARK_FLUSH       0x00000080
#define LINUX_FAN_MARK_FILESYSTEM  0x00000100
#define LINUX_FAN_MARK_EVICTABLE   0x00000200
#define LINUX_FAN_MARK_IGNORE      0x00000400
#define LINUX_FAN_MARK_KNOWN \
    (LINUX_FAN_MARK_ADD | LINUX_FAN_MARK_REMOVE | \
     LINUX_FAN_MARK_DONT_FOLLOW | LINUX_FAN_MARK_ONLYDIR | \
     LINUX_FAN_MARK_MOUNT | LINUX_FAN_MARK_IGNORED_MASK | \
     LINUX_FAN_MARK_IGNORED_SURV_MODIFY | LINUX_FAN_MARK_FLUSH | \
     LINUX_FAN_MARK_FILESYSTEM | LINUX_FAN_MARK_EVICTABLE | \
     LINUX_FAN_MARK_IGNORE)

int64_t linux_fanotify_init(uint32_t flags, uint32_t event_f_flags);
int64_t linux_fanotify_mark(int fan_fd, uint32_t flags,
                            uint64_t mask, int dirfd,
                            const char *pathname);
int64_t linux_fanotify_close(int fd);
int64_t linux_fanotify_read(int fd, void *buf, size_t len);
int64_t linux_fanotify_write(int fd, const void *buf, size_t len);
int64_t linux_fanotify_lseek(int fd, int64_t offset, int whence);

void linux_fanotify_register_syscalls(void);
void linux_fanotify_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FANOTIFY_H */
