#ifndef KERNEL_LINUX_COMPAT_LINUX_POSIX_TIMER_H
#define KERNEL_LINUX_COMPAT_LINUX_POSIX_TIMER_H

/* Linux ABI POSIX timer syscalls.
 *
 *   int timer_create   (clockid_t clockid, struct sigevent *sevp,
 *                        timer_t *timerid);
 *   int timer_settime  (timer_t timerid, int flags,
 *                        const struct itimerspec *new_value,
 *                        struct itimerspec *old_value);
 *   int timer_gettime  (timer_t timerid, struct itimerspec *curr_value);
 *   int timer_getoverrun(timer_t timerid);
 *   int timer_delete   (timer_t timerid);
 *
 * Why this matters for the Firefox port:
 *   - Firefox profiler uses POSIX timers (timer_create with
 *     SIGEV_THREAD) to sample stacks at fixed intervals.
 *     -ENOSYS disables the profiler entirely.
 *   - SpiderMonkey GC heuristics use timer_create with
 *     CLOCK_MONOTONIC to run incremental marking on a wall
 *     clock. Without it, GC falls back to per-allocation
 *     barriers (slower).
 *   - musl `timer_create` is implemented via the kernel
 *     syscall directly; no userspace fallback exists.
 *
 * Linux x86_64 layout:
 *
 *   typedef int timer_t;  // kernel returns int via *timerid
 *   struct timespec  { time_t tv_sec; long tv_nsec; };
 *   struct itimerspec {
 *       struct timespec it_interval;   // 16 bytes
 *       struct timespec it_value;
 *   };
 *
 * Marco M1 has no per-task signal delivery yet, so SIGEV_SIGNAL
 * timers don't actually fire. We implement the storage ABI:
 *   - 16-slot timer table, ids 1..16 (Linux kernel encodes
 *     struct k_itimer pointers as compact ints; we hand back
 *     small positive ints).
 *   - timer_create stores clockid + sigev info, returns id.
 *   - timer_settime stores it_interval and it_value, validates
 *     tv_nsec ranges, supports TIMER_ABSTIME flag.
 *   - timer_gettime reads back what was stored (no countdown
 *     yet; per-task signal subsystem will own that later).
 *   - timer_getoverrun returns 0 (no overruns recorded).
 *   - timer_delete frees the slot.
 *
 * This satisfies the Firefox profiler probe and SpiderMonkey
 * GC heuristic (both detect failure via -ENOSYS, so success
 * here lets them take their fast path even if the timer
 * doesn't actually fire). */

#include <stdint.h>
#include <stddef.h>

#define LINUX_TIMER_ABSTIME      0x01

#define LINUX_POSIX_TIMER_MAX    16

/* Subset of clockids accepted by timer_create. We accept the
 * common ones; unknown -> -EINVAL. Mirror values from
 * include/kernel/linux_compat/linux_clock.h if available. */
#define LINUX_CLOCK_REALTIME           0
#define LINUX_CLOCK_MONOTONIC          1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID  3
#define LINUX_CLOCK_MONOTONIC_RAW      4
#define LINUX_CLOCK_REALTIME_COARSE    5
#define LINUX_CLOCK_MONOTONIC_COARSE   6
#define LINUX_CLOCK_BOOTTIME           7

struct linux_timespec_pt {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct linux_itimerspec {
    struct linux_timespec_pt it_interval;
    struct linux_timespec_pt it_value;
};

/* Linux <bits/siginfo.h> sigevent. We accept only the fields
 * we care about; the actual struct is much larger but the
 * trailing union is opaque from a syscall ABI perspective. */
#define LINUX_SIGEV_SIGNAL    0
#define LINUX_SIGEV_NONE      1
#define LINUX_SIGEV_THREAD    2
#define LINUX_SIGEV_THREAD_ID 4

struct linux_sigevent_subset {
    int32_t sigev_notify;
    int32_t sigev_signo;
    /* Larger payload elided; we don't dereference it. */
};

int64_t linux_timer_create(int clockid, void *sevp, int *timerid);
int64_t linux_timer_settime(int timerid, int flags,
                            const struct linux_itimerspec *new_value,
                            struct linux_itimerspec *old_value);
int64_t linux_timer_gettime(int timerid,
                            struct linux_itimerspec *curr_value);
int64_t linux_timer_getoverrun(int timerid);
int64_t linux_timer_delete(int timerid);

void linux_posix_timer_register_syscalls(void);
void linux_posix_timer_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_POSIX_TIMER_H */
