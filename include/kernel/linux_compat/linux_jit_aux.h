#ifndef KERNEL_LINUX_COMPAT_LINUX_JIT_AUX_H
#define KERNEL_LINUX_COMPAT_LINUX_JIT_AUX_H

/* Linux ABI JIT-auxiliary syscalls.
 *
 *   int membarrier      (int cmd, unsigned int flags, int cpu_id);
 *   int userfaultfd     (int flags);
 *   int sched_rr_get_interval(pid_t pid, struct timespec *tp);
 *
 * Why this matters for the Firefox port:
 *   - SpiderMonkey JIT (IonMonkey + WebAssembly) calls
 *     membarrier(MEMBARRIER_CMD_PRIVATE_EXPECTED_SYNC) before
 *     flipping a code page from RW to RX. Without it, the
 *     JIT falls back to mprotect-twice (slow) and contends
 *     with other threads' execution.
 *   - Userland WASM page-fault handlers use userfaultfd to
 *     implement guard-page-style trap handling for the
 *     wasm-on-linux memory model. -ENOSYS makes WASM fall
 *     back to a slower bounds-check JIT.
 *   - musl pthread_attr_getschedparam reads
 *     sched_rr_get_interval(SCHED_RR) to size its time-slice
 *     budget; -ENOSYS makes it default to 100 ms (too coarse).
 *
 * Linux semantics:
 *   - membarrier query (cmd == MEMBARRIER_CMD_QUERY) returns a
 *     bitmask of supported commands. We report support for
 *     PRIVATE_EXPEDITED + PRIVATE_EXPEDITED_SYNC_CORE, which
 *     are what SpiderMonkey JIT actually calls.
 *   - userfaultfd returns a fd >= 0 on success. We allocate
 *     fds from a small table (Marco M1 has no actual page-
 *     fault routing yet, so the fd is a placeholder that
 *     userland can read/poll on).
 *   - sched_rr_get_interval returns the RR time-slice in
 *     `tp`. Linux defaults to 0.1s; we report a single tick. */

#include <stdint.h>
#include <stddef.h>

/* membarrier commands (linux/membarrier.h). */
#define LINUX_MEMBARRIER_CMD_QUERY                          0
#define LINUX_MEMBARRIER_CMD_GLOBAL                         (1 << 0)
#define LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED               (1 << 1)
#define LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED      (1 << 2)
#define LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED              (1 << 3)
#define LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED     (1 << 4)
#define LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE    (1 << 5)
#define LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE (1 << 6)

#define LINUX_MEMBARRIER_SUPPORTED \
    (LINUX_MEMBARRIER_CMD_GLOBAL | \
     LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED | \
     LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED | \
     LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED | \
     LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED | \
     LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE | \
     LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE)

/* membarrier flags. */
#define LINUX_MEMBARRIER_FLAG_CPU                           (1u << 0)
#define LINUX_MEMBARRIER_FLAG_KNOWN                         LINUX_MEMBARRIER_FLAG_CPU

/* userfaultfd flags. */
#define LINUX_UFFD_USER_MODE_ONLY    1u
#define LINUX_UFFD_NONBLOCK          0x800u   /* O_NONBLOCK */
#define LINUX_UFFD_CLOEXEC           0x80000u /* O_CLOEXEC */
#define LINUX_UFFD_KNOWN_FLAGS \
    (LINUX_UFFD_USER_MODE_ONLY | LINUX_UFFD_NONBLOCK | \
     LINUX_UFFD_CLOEXEC)

/* userfaultfd fd encoding base; placed between epoll and inotify. */
#define LINUX_UFFD_FD_BASE  0xA800
#define LINUX_UFFD_FD_MAX   16

struct linux_jit_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

int64_t linux_membarrier(int cmd, uint32_t flags, int cpu_id);
int64_t linux_userfaultfd(int flags);
int64_t linux_userfaultfd_close(int fd);
int64_t linux_userfaultfd_read(int fd, void *buf, size_t len);
int64_t linux_userfaultfd_write(int fd, const void *buf, size_t len);
int64_t linux_userfaultfd_lseek(int fd, int64_t offset, int whence);
int64_t linux_sched_rr_get_interval(int pid,
                                    struct linux_jit_timespec *tp);

void linux_jit_aux_register_syscalls(void);
void linux_jit_aux_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_JIT_AUX_H */
