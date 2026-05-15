#ifndef KERNEL_LINUX_COMPAT_LINUX_ITIMER_H
#define KERNEL_LINUX_COMPAT_LINUX_ITIMER_H

/* Linux ABI interval-timer + alarm + process-times syscalls.
 *
 *   unsigned int alarm(unsigned int seconds);
 *   int  getitimer(int which, struct itimerval *curr_value);
 *   int  setitimer(int which, const struct itimerval *new_value,
 *                  struct itimerval *old_value);
 *   clock_t times(struct tms *buf);
 *
 * Why this matters for the Firefox port:
 *   - musl's `sigtimedwait()` fallback uses alarm to bound the
 *     wait; -ENOSYS makes it spin instead.
 *   - Firefox's compositor watchdog uses ITIMER_REAL via
 *     setitimer to detect a frozen render thread; -ENOSYS
 *     disables crash-on-hang detection.
 *   - bash, ps, and `time(1)` use times(2) for "user / system
 *     time" reporting; -ENOSYS makes them report 0/0.
 *
 * Linux x86_64 layout:
 *
 *   struct itimerval {
 *       struct timeval it_interval;   // 16 bytes (sec + usec)
 *       struct timeval it_value;
 *   };
 *   struct timeval { long tv_sec; long tv_usec; };
 *   struct tms {
 *       clock_t tms_utime;   // user time
 *       clock_t tms_stime;   // system time
 *       clock_t tms_cutime;  // children user time
 *       clock_t tms_cstime;  // children system time
 *   };
 *   clock_t = long; sysconf(_SC_CLK_TCK) = 100 on Linux.
 *
 * Marco M1 has no per-task accounting yet, so:
 *   - alarm: store the requested seconds in module-local state,
 *     return the previous request (Linux semantics). No actual
 *     SIGALRM delivery yet (signal subsystem is storage-only).
 *   - getitimer/setitimer: validate `which` whitelist, store/load
 *     itimerval values from a 3-slot module table. No signal
 *     fires (subsystem hooks pending).
 *   - times: returns (current_ticks, 0, 0, 0). Provider can
 *     inject real per-task CPU accounting later. */

#include <stdint.h>
#include <stddef.h>

/* Linux: setitimer/getitimer "which" values. */
#define LINUX_ITIMER_REAL     0
#define LINUX_ITIMER_VIRTUAL  1
#define LINUX_ITIMER_PROF     2

#define LINUX_ITIMER_COUNT    3

/* Linux: sysconf(_SC_CLK_TCK) is fixed at 100 on most arches.
 * `times()` reports tick counts based on this. */
#define LINUX_CLK_TCK         100

struct linux_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct linux_itimerval {
    struct linux_timeval it_interval;
    struct linux_timeval it_value;
};

struct linux_tms {
    int64_t tms_utime;
    int64_t tms_stime;
    int64_t tms_cutime;
    int64_t tms_cstime;
};

struct linux_itimer_ops {
    /* Returns the current monotonic tick count for `times()`.
     * NULL = caller falls back to 0. */
    int64_t (*now_ticks)(void);
};

void linux_itimer_install_ops(const struct linux_itimer_ops *ops);
void linux_itimer_reset_for_tests(void);

uint32_t linux_alarm(uint32_t seconds);
int64_t  linux_getitimer(int which, struct linux_itimerval *curr_value);
int64_t  linux_setitimer(int which,
                         const struct linux_itimerval *new_value,
                         struct linux_itimerval *old_value);
int64_t  linux_times(struct linux_tms *buf);

void linux_itimer_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_ITIMER_H */
