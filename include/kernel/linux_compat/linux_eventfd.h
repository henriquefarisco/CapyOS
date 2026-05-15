#ifndef KERNEL_LINUX_COMPAT_LINUX_EVENTFD_H
#define KERNEL_LINUX_COMPAT_LINUX_EVENTFD_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `eventfd2(2)`, `signalfd4(2)`, `timerfd_create(2)`,
 * `timerfd_settime(2)` shims (S1.7).
 *
 * Marco M1 surface:
 *
 *   eventfd2(initval, flags)             -- counter fd
 *      flags: EFD_CLOEXEC|EFD_NONBLOCK|EFD_SEMAPHORE
 *
 *   signalfd4(fd, mask, sizemask, flags) -- signals as fd
 *      Storage-only fd: mask can be created/updated; reads return
 *      -EAGAIN until signal delivery lands.
 *
 *   timerfd_create(clockid, flags)       -- timer fd
 *      Functional one-shot/periodic timer fd backed by linux_clock.
 *
 *   timerfd_settime(fd, flags, new, old) -- arm/disarm timer
 */

/* Linux flags (uapi/linux/eventfd.h). */
#define LINUX_EFD_SEMAPHORE 0x1u
/* These map to O_NONBLOCK / O_CLOEXEC at libc level but Linux
 * uses different bit positions. Match upstream. */
#define LINUX_EFD_CLOEXEC   0x080000u  /* same bit as O_CLOEXEC  */
#define LINUX_EFD_NONBLOCK  0x000800u  /* same bit as O_NONBLOCK */

#define LINUX_EFD_KNOWN_FLAGS \
    (LINUX_EFD_SEMAPHORE | LINUX_EFD_CLOEXEC | LINUX_EFD_NONBLOCK)

/* Linux signalfd4 flags (uapi/linux/signalfd.h). */
#define LINUX_SFD_NONBLOCK 0x000800u
#define LINUX_SFD_CLOEXEC  0x080000u
#define LINUX_SFD_KNOWN_FLAGS (LINUX_SFD_NONBLOCK | LINUX_SFD_CLOEXEC)
#define LINUX_SIGNALFD_MAX 16
#define LINUX_SIGNALFD_FD_BASE 0x4400
#define LINUX_SIGNALFD_SIGINFO_SIZE 128u

/* Linux timerfd_create flags (uapi/linux/timerfd.h). */
#define LINUX_TFD_NONBLOCK 0x000800u
#define LINUX_TFD_CLOEXEC  0x080000u
#define LINUX_TFD_KNOWN_FLAGS (LINUX_TFD_NONBLOCK | LINUX_TFD_CLOEXEC)

/* Linux clockid values accepted by timerfd_create. */
#define LINUX_CLOCK_REALTIME  0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_BOOTTIME  7

/* eventfd state. The shim stores at most LINUX_EVENTFD_MAX
 * concurrent eventfds in a per-module table. The fd returned to
 * userland is index + LINUX_EVENTFD_FD_BASE so it cannot collide
 * with kernel pipe ids (PIPE_MAX < 256, so we use 0x4000+). */
#define LINUX_EVENTFD_MAX     32
#define LINUX_EVENTFD_FD_BASE 0x4000

/* Ops for the kernel side: caller-provided fd allocator. The
 * shim itself owns the counter table. */
struct linux_eventfd_ops {
    /* Allocate a kernel-visible fd that maps back to the eventfd
     * slot. May return -1 if the kernel fd table is full. The
     * `slot` argument is the index into the eventfd table; the
     * implementation may use it to remember the mapping. NULL is
     * permitted: shim falls back to returning the slot index +
     * LINUX_EVENTFD_FD_BASE directly. */
    int (*alloc_fd)(int slot, uint32_t flags);
};

void linux_eventfd_install_ops(const struct linux_eventfd_ops *ops);
void linux_eventfd_reset_for_tests(void);

/* eventfd2 entry. Returns the new fd, or -LINUX_E*. */
int64_t linux_eventfd2(uint64_t initval, uint32_t flags);

/* Read 8 bytes from an eventfd. Caller passes the fd returned by
 * eventfd2; the shim resolves the slot and updates the counter.
 * Linux semantics:
 *   normal mode:    returns counter, resets to 0.
 *   semaphore mode: returns 1, decrements counter by 1.
 *   counter == 0:   returns -LINUX_EAGAIN (NONBLOCK) or blocks
 *                   (only EAGAIN supported here -- block path
 *                   would require integration with sched).
 *   len < 8: -LINUX_EINVAL; buf == NULL: -LINUX_EFAULT.
 *   fd not an eventfd: -LINUX_EBADF.
 */
int64_t linux_eventfd_read(int fd, uint64_t *out, size_t len);

/* Write 8 bytes to an eventfd. Adds `value` to the counter.
 * Linux semantics:
 *   value == 0xFFFFFFFFFFFFFFFF: -LINUX_EINVAL.
 *   counter overflow: -LINUX_EAGAIN (NONBLOCK) or block.
 *   Otherwise: returns 8.
 */
int64_t linux_eventfd_write(int fd, uint64_t value, size_t len);

int64_t linux_signalfd4(int fd, uint64_t mask_ptr, size_t sizemask,
                        uint32_t flags);
int64_t linux_signalfd_read(int fd, void *out, size_t len);
int64_t linux_eventfd_family_close(int fd);
int64_t linux_eventfd_family_read(int fd, void *buf, size_t len);
int64_t linux_eventfd_family_write(int fd, const void *buf, size_t len);
int64_t linux_eventfd_family_lseek(int fd, int64_t offset, int whence);
uint32_t linux_eventfd_family_poll_events(int fd);

/* timerfd table sizing. fd encoding 0x4800+slot to avoid collision
 * with eventfd (0x4000+slot). */
#define LINUX_TIMERFD_MAX_INSTANCES 16
#define LINUX_TIMERFD_FD_BASE 0x4800

/* Linux struct itimerspec subset (uapi/linux/time.h). Fields are
 * struct timespec (sec + nsec). */
struct linux_itimerspec {
    int64_t it_interval_sec;
    int64_t it_interval_nsec;
    int64_t it_value_sec;
    int64_t it_value_nsec;
};

/* timerfd_settime flag bits. */
#define LINUX_TFD_TIMER_ABSTIME 0x1u
#define LINUX_TFD_TIMER_CANCEL_ON_SET 0x2u
#define LINUX_TFD_SETTIME_KNOWN_FLAGS \
    (LINUX_TFD_TIMER_ABSTIME | LINUX_TFD_TIMER_CANCEL_ON_SET)

/* Now-ns oracle: returns current clock value in nanoseconds.
 * Used by timerfd to compute expiration counts. The eventfd init
 * wires this to linux_clock so production gets monotonic time;
 * tests inject a deterministic counter. */
typedef uint64_t (*linux_eventfd_now_ns_fn)(void);

void linux_eventfd_install_now_ns(linux_eventfd_now_ns_fn fn);

int64_t linux_timerfd_create(int clockid, uint32_t flags);
int64_t linux_timerfd_settime(int fd, int flags,
                              const struct linux_itimerspec *new_val,
                              struct linux_itimerspec *old_val);
int64_t linux_timerfd_gettime(int fd, struct linux_itimerspec *cur);

/* Functional read: returns the number of expirations since the last
 * read (Linux contract). 0 expirations -> -EAGAIN. */
int64_t linux_timerfd_read(int fd, uint64_t *out, size_t len);

void linux_eventfd_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_EVENTFD_H */
