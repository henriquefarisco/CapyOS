#ifndef KERNEL_LINUX_COMPAT_LINUX_MODERN_MISC_H
#define KERNEL_LINUX_COMPAT_LINUX_MODERN_MISC_H

/* Linux ABI modern miscellaneous syscalls.
 *
 *   long futex_waitv (struct futex_waitv *waiters,
 *                      uint32_t nr_futexes, uint32_t flags,
 *                      struct __kernel_timespec *timeout,
 *                      clockid_t clockid);
 *   int  clock_adjtime(clockid_t clk_id, struct timex *buf);
 *   int  memfd_secret(unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - musl 1.2.4+ pthread mutex/cond may try futex_waitv when
 *     waiting on multiple futexes simultaneously. -ENOSYS makes
 *     it fall back to single-futex FUTEX_WAIT in a loop.
 *   - chrony / systemd-timesyncd use clock_adjtime to slew the
 *     RTC; -ENOSYS makes them skip slewing.
 *   - libsecret on Linux 5.14+ allocates secret memory via
 *     memfd_secret to store credential bufferss that the kernel
 *     scrubs from the page cache on close.
 *
 * Linux semantics:
 *   - futex_waitv: nr_futexes <= FUTEX_WAITV_MAX (128); flags
 *     must be 0; clockid must be REALTIME or MONOTONIC.
 *   - clock_adjtime: needs CAP_SYS_TIME for write-mode; reads
 *     are always allowed and return the kernel's current time
 *     adjustment state.
 *   - memfd_secret: returns a fd that backs a private mapping
 *     unreadable from /proc/self/mem and unmappable into
 *     other processes.
 *
 * Marco M1: futex_waitv returns -ENOSYS so musl loops on
 * single-futex FUTEX_WAIT (which we have). clock_adjtime
 * returns the current state (TIME_OK with zeros) for reads,
 * accepts writes as no-op. memfd_secret allocates a fd from a
 * small pool. */

#include <stdint.h>
#include <stddef.h>

/* futex_waitv (uapi/linux/futex.h, Linux 5.16+). */
#define LINUX_FUTEX_WAITV_MAX     128

/* clockids (subset). */
#define LINUX_FX_CLOCK_REALTIME       0
#define LINUX_FX_CLOCK_MONOTONIC      1

/* clock_adjtime "modes" / status. */
#define LINUX_TIME_OK              0
#define LINUX_TIME_INS             1  /* insert leap second */
#define LINUX_TIME_DEL             2  /* delete leap second */
#define LINUX_TIME_OOP             3  /* leap second in progress */
#define LINUX_TIME_WAIT            4  /* leap second has occurred */
#define LINUX_TIME_ERROR           5  /* clock not synchronised */

/* timex modes (we accept the read-only no-op subset). */
#define LINUX_TIMEX_MOD_NONE       0  /* read-only */
#define LINUX_TIMEX_MOD_KNOWN      0xFFFu  /* upper 4 bits forbidden */

/* memfd_secret flags. */
#define LINUX_MEMFD_SECRET_CLOEXEC 0x00000001
#define LINUX_MEMFD_SECRET_KNOWN   LINUX_MEMFD_SECRET_CLOEXEC

#define LINUX_MEMFD_SECRET_FD_BASE 0xD000
#define LINUX_MEMFD_SECRET_FD_MAX  8

/* Subset of struct timex relevant for our read path. The full
 * struct is large (208 bytes on x86_64); we only need to write
 * the leading "modes" field for read-mode validation and not
 * dereference the rest. */
struct linux_timex_subset {
    uint32_t modes;
    /* opaque tail; we don't dereference it. */
};

int64_t linux_futex_waitv(void *waiters, uint32_t nr_futexes,
                          uint32_t flags, void *timeout,
                          int clockid);
int64_t linux_clock_adjtime(int clk_id, struct linux_timex_subset *buf);
int64_t linux_memfd_secret(uint32_t flags);
int64_t linux_memfd_secret_close(int fd);
int64_t linux_memfd_secret_read(int fd, void *buf, size_t len);
int64_t linux_memfd_secret_write(int fd, const void *buf, size_t len);
int64_t linux_memfd_secret_lseek(int fd, int64_t offset, int whence);

void linux_modern_misc_register_syscalls(void);
void linux_modern_misc_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_MODERN_MISC_H */
