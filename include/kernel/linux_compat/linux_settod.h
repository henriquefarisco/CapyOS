#ifndef KERNEL_LINUX_COMPAT_LINUX_SETTOD_H
#define KERNEL_LINUX_COMPAT_LINUX_SETTOD_H

/* Linux ABI legacy time-of-day setter syscall.
 *
 *   int settimeofday(const struct timeval *tv,
 *                    const struct timezone *tz);
 *
 * Why this matters for the Firefox port:
 *   - musl `clock_settime(CLOCK_REALTIME)` falls back to
 *     settimeofday on older kernels; -ENOSYS makes the
 *     time-set path return failure.
 *   - ntpdate / chrony / systemd-timesyncd-style daemons
 *     poke time via settimeofday.
 *   - Firefox doesn't normally call this -- but the failure
 *     handler in glibc emits a warning that pollutes logs.
 *
 * Linux semantics:
 *   - Requires CAP_SYS_TIME; we accept (single-root world).
 *   - tz parameter is deprecated; modern userland passes NULL.
 *     Linux ignores tz->tz_minuteswest entirely.
 *   - tv->tv_usec must be in [0, 1e6); else -EINVAL.
 *
 * Marco M1 has no real wall-clock state to write; we accept
 * the call as a no-op success when well-formed. The provider
 * pattern lets a future RTC driver intercept and persist. */

#include <stdint.h>
#include <stddef.h>

struct linux_settod_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct linux_settod_ops {
    /* Optional callback. NULL = no-op success. */
    int64_t (*set_seconds)(int64_t seconds, int64_t microseconds);
};

void linux_settod_install_ops(const struct linux_settod_ops *ops);
void linux_settod_reset_for_tests(void);

int64_t linux_settimeofday(const struct linux_settod_timeval *tv,
                           const void *tz);

void linux_settod_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SETTOD_H */
