#ifndef KERNEL_LINUX_COMPAT_LINUX_MEMFD_H
#define KERNEL_LINUX_COMPAT_LINUX_MEMFD_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI memfd_create + pidfd_* shim (S1.15).
 *
 * Marco M1 surface:
 *
 *   memfd_create(name, flags)         -- in-memory file fd
 *   pidfd_open(pid, flags)            -- fd referring to a pid
 *   pidfd_send_signal(pidfd, sig, info, flags) -- send signal via pidfd
 *
 * Why this matters for Firefox:
 *   - Chromium IPC sandbox uses memfd_create for shared memory
 *     between content/parent processes.
 *   - pidfd_open / pidfd_send_signal are the modern, race-free
 *     way to operate on pids; the sandbox uses them for child
 *     supervision (Linux 5.3+).
 *
 * Marco M1 status:
 *   - memfd_create allocates an in-memory slot (table of 16),
 *     stores the name, returns a fd encoded as 0x5000+slot. The
 *     actual ftruncate/mmap/read/write integration lives in
 *     future work; the fd just exists so userland startup paths
 *     succeed.
 *   - pidfd_open validates flags and pid (checked via injected
 *     callback). If the pid exists, returns 0x5800+pid_slot.
 *   - pidfd_send_signal validates flags+sig and returns
 *     -ENOSYS until the signal delivery infra (S1.12) lands;
 *     before that surfaces -EINVAL/-EBADF for malformed input.
 */

/* memfd_create flags (uapi/linux/memfd.h). */
#define LINUX_MFD_CLOEXEC       0x0001u
#define LINUX_MFD_ALLOW_SEALING 0x0002u
#define LINUX_MFD_HUGETLB       0x0004u
#define LINUX_MFD_NOEXEC_SEAL   0x0008u
#define LINUX_MFD_EXEC          0x0010u
#define LINUX_MFD_KNOWN_FLAGS \
    (LINUX_MFD_CLOEXEC | LINUX_MFD_ALLOW_SEALING | LINUX_MFD_HUGETLB | \
     LINUX_MFD_NOEXEC_SEAL | LINUX_MFD_EXEC)

/* memfd name limit -- Linux: 249 chars excluding "memfd:" prefix. */
#define LINUX_MEMFD_NAME_MAX 249u

/* pidfd_open flags (uapi/linux/pidfd.h). */
#define LINUX_PIDFD_NONBLOCK 0x000800u  /* same bit as O_NONBLOCK */
#define LINUX_PIDFD_KNOWN_FLAGS LINUX_PIDFD_NONBLOCK

/* pidfd_send_signal flags. Linux currently: 0 only. */
#define LINUX_PIDFD_SS_KNOWN_FLAGS 0u

/* fd encoding ranges. */
#define LINUX_MEMFD_FD_BASE       0x5000
#define LINUX_MEMFD_MAX_INSTANCES 16
#define LINUX_PIDFD_FD_BASE       0x5800
#define LINUX_PIDFD_MAX_INSTANCES 16

/* Ops bundle. pid_exists is consulted by pidfd_open to validate. */
struct linux_memfd_ops {
    int (*pid_exists)(uint32_t pid);
};

void linux_memfd_install_ops(const struct linux_memfd_ops *ops);
void linux_memfd_reset_for_tests(void);

int64_t linux_memfd_create(uint64_t name_ptr, uint32_t flags);
int64_t linux_pidfd_open(uint32_t pid, uint32_t flags);
int64_t linux_pidfd_send_signal(int pidfd, int sig,
                                uint64_t info_ptr, uint32_t flags);
int64_t linux_memfd_family_close(int fd);
int64_t linux_memfd_family_read(int fd, void *buf, size_t len);
int64_t linux_memfd_family_write(int fd, const void *buf, size_t len);
int64_t linux_memfd_family_lseek(int fd, int64_t offset, int whence);

void linux_memfd_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_MEMFD_H */
