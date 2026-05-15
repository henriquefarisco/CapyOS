#ifndef KERNEL_LINUX_COMPAT_LINUX_CLOCK_H
#define KERNEL_LINUX_COMPAT_LINUX_CLOCK_H

#include <stdint.h>
#include "kernel/linux_compat/linux_types.h"

/* Linux-ABI `clock_gettime` shim.
 *
 * This module exposes the kernel-side handler that Firefox/SpiderMonkey
 * expects when calling `clock_gettime(2)` from
 * `mfbt/TimeStamp_posix.cpp` and `js/src/threading/posix/PosixThread.cpp`.
 *
 * Layering (host-testable design):
 *
 *   linux_clock_compute_timespec()  pure arithmetic, no globals
 *           ^
 *           |
 *   linux_clock_gettime()           dispatch on `clk` + read kernel
 *                                   timebase via callbacks installed
 *                                   at boot
 *
 * The arithmetic is split out so host unit tests can drive it with
 * synthetic inputs (no TSC, no APIC) and still cover overflow / edge
 * cases. The kernel build wires real callbacks in
 * `src/kernel/linux_compat/linux_clock_init.c`.
 *
 * Return convention: 0 on success, negative Linux errno on failure
 * (-LINUX_EFAULT, -LINUX_EINVAL, ...).
 */

/* Pure arithmetic: convert an elapsed-cycles count and a TSC frequency
 * (in Hz) into a `struct linux_timespec`. Defensive against
 * `tsc_hz == 0` (returns -LINUX_EINVAL). Overflow-safe up to
 * `~2^64 / NSEC_PER_SEC` seconds, far beyond any realistic uptime. */
int linux_clock_compute_timespec(uint64_t elapsed_cycles,
                                 uint64_t tsc_hz,
                                 struct linux_timespec *out);

/* Add two `struct linux_timespec` values, normalising tv_nsec into
 * [0, NSEC_PER_SEC). Defensive against NULL pointers. Used to compute
 * CLOCK_REALTIME = wall_epoch_offset + monotonic_elapsed. */
int linux_clock_add_timespec(const struct linux_timespec *a,
                             const struct linux_timespec *b,
                             struct linux_timespec *out);

/* Kernel-side time source callbacks. Set once at boot; never NULL
 * thereafter. Tests inject deterministic values here. */
typedef uint64_t (*linux_clock_cycles_fn)(void);
typedef uint64_t (*linux_clock_hz_fn)(void);

/* Install the cycles + hz sources. Subsequent calls overwrite the
 * previous bindings; no locking (boot-time setup). */
void linux_clock_install_timebase(linux_clock_cycles_fn cycles,
                                  linux_clock_hz_fn hz,
                                  uint64_t tsc_start);

/* Install the wall-clock epoch (CLOCK_REALTIME = wall + monotonic).
 * `wall_seconds_at_boot` and `wall_nanoseconds_at_boot` are the wall
 * clock value at the moment `tsc_start` was sampled. If never
 * installed, CLOCK_REALTIME falls back to CLOCK_MONOTONIC + 0. */
void linux_clock_install_wall_epoch(int64_t wall_seconds_at_boot,
                                    int64_t wall_nanoseconds_at_boot);

/* Reset all installed state to defaults. Test-only; called from
 * tests/test_linux_clock.c between scenarios. */
void linux_clock_reset_for_tests(void);

/* Register clock-related syscalls in the linux_syscall dispatcher
 * table. Called once from `linux_syscall_init()` at boot. Pure
 * registration -- the timebase must already have been installed via
 * `linux_clock_install_timebase()` for the handler to return real
 * values; otherwise it returns sec=0, nsec=0 (graceful pre-init). */
void linux_clock_register_syscalls(void);

/* `gettimeofday(struct timeval *tv, struct timezone *tz)` --
 * legacy time syscall. Linux ignores `tz` (timezone always NULL on
 * modern systems). We populate `tv` from `clock_gettime(REALTIME)`.
 * Returns 0 on success, -LINUX_EFAULT if `tv` is NULL. */
struct linux_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
int64_t linux_gettimeofday(struct linux_timeval *tv);

/* `nanosleep(const struct timespec *req, struct timespec *rem)` --
 * sleep for the requested duration. Linux semantics:
 *   - sec/nsec must be non-negative
 *   - nsec < 1_000_000_000
 *   - on success rem is unchanged (kernel may zero it); on early
 *     wake by signal, rem holds the remaining time
 * Returns 0 on completion, -LINUX_EINVAL on bad args, -LINUX_EFAULT
 * on NULL req.
 *
 * Marco M1: CapyOS does not yet have a sleep primitive accessible
 * from this layer (`task_sleep_until` lands later). For now we
 * spin-busy-wait against `clock_gettime(MONOTONIC)` to advance --
 * deterministic and host-testable, but expensive. Userland that
 * sleeps for milliseconds gets correct elapsed time; userland that
 * sleeps for seconds blocks the whole CPU. Refines later when a
 * real kernel sleep API is wired. */
int64_t linux_nanosleep(const struct linux_timespec *req,
                        struct linux_timespec *rem);

/* `clock_nanosleep(clockid, flags, req, rem)` -- per-clock sleep
 * with optional absolute deadline (TIMER_ABSTIME = 1).
 *
 * Linux semantics:
 *   - clk in {CLOCK_MONOTONIC, CLOCK_REALTIME, CLOCK_BOOTTIME}
 *     are accepted; CPUTIME variants -> -ENOTSUP today.
 *   - flags == 0  : `req` is a relative duration (= nanosleep).
 *   - flags & TIMER_ABSTIME: `req` is an absolute deadline on
 *     the chosen clock; `rem` is ignored.
 *   - Negative tv_sec / out-of-range tv_nsec -> -EINVAL.
 *
 * Marco M1 reuses the same spin-wait as nanosleep. */
#define LINUX_TIMER_ABSTIME 1
int64_t linux_clock_nanosleep(int32_t clockid, int flags,
                              const struct linux_timespec *req,
                              struct linux_timespec *rem);

/* Linux-ABI `clock_gettime(clk_id, struct timespec *tp)`. Returns
 * 0 on success, negative Linux errno on failure:
 *   -LINUX_EFAULT  out == NULL
 *   -LINUX_EINVAL  unknown clk
 *   -LINUX_ENOSYS  clk not yet implemented (e.g. CPUTIME variants)
 *
 * Supported now:
 *   LINUX_CLOCK_MONOTONIC      / LINUX_CLOCK_MONOTONIC_RAW
 *   LINUX_CLOCK_MONOTONIC_COARSE
 *   LINUX_CLOCK_BOOTTIME
 *   LINUX_CLOCK_REALTIME       / LINUX_CLOCK_REALTIME_COARSE
 *
 * Not yet supported (returns -LINUX_ENOSYS):
 *   LINUX_CLOCK_PROCESS_CPUTIME_ID
 *   LINUX_CLOCK_THREAD_CPUTIME_ID  */
int linux_clock_gettime(linux_clockid_t clk, struct linux_timespec *out);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CLOCK_H */
