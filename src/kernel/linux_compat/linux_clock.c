#include "kernel/linux_compat/linux_clock.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_types.h"

#include <stdint.h>
#include <stddef.h>

/* Module-private state. Installed once at boot via the
 * `linux_clock_install_*` setters; tests reset via
 * `linux_clock_reset_for_tests()`. */
static linux_clock_cycles_fn g_cycles_fn = NULL;
static linux_clock_hz_fn     g_hz_fn     = NULL;
static uint64_t              g_tsc_start = 0;
static int64_t               g_wall_seconds = 0;
static int64_t               g_wall_nanoseconds = 0;
static int                   g_wall_installed = 0;

/* ------------------------------------------------------------------ */
/* Pure arithmetic core.                                              */
/* ------------------------------------------------------------------ */

int linux_clock_compute_timespec(uint64_t elapsed_cycles,
                                 uint64_t tsc_hz,
                                 struct linux_timespec *out) {
  if (!out) return -LINUX_EFAULT;
  if (tsc_hz == 0) return -LINUX_EINVAL;

  /* Decompose into seconds + remainder cycles before multiplying by
   * 1e9. With tsc_hz <= 4 GHz the worst-case `remainder * 1e9` is
   * 4e9 * 1e9 = 4e18 which fits in uint64 (max ~1.8e19). Skipping
   * this decomposition would overflow after a few seconds of uptime
   * with naive `(cycles * 1e9) / hz`. */
  uint64_t seconds   = elapsed_cycles / tsc_hz;
  uint64_t remainder = elapsed_cycles % tsc_hz;
  uint64_t nanoseconds = (remainder * LINUX_NSEC_PER_SEC) / tsc_hz;

  /* `nanoseconds` is in [0, NSEC_PER_SEC) by construction; no
   * carry into seconds is required. Defensive clamp anyway. */
  if (nanoseconds >= LINUX_NSEC_PER_SEC) {
    seconds += nanoseconds / LINUX_NSEC_PER_SEC;
    nanoseconds %= LINUX_NSEC_PER_SEC;
  }

  out->tv_sec  = (linux_time_t)seconds;
  out->tv_nsec = (int64_t)nanoseconds;
  return 0;
}

int linux_clock_add_timespec(const struct linux_timespec *a,
                             const struct linux_timespec *b,
                             struct linux_timespec *out) {
  if (!a || !b || !out) return -LINUX_EFAULT;

  int64_t sec  = a->tv_sec + b->tv_sec;
  int64_t nsec = a->tv_nsec + b->tv_nsec;

  if (nsec >= (int64_t)LINUX_NSEC_PER_SEC) {
    sec  += nsec / (int64_t)LINUX_NSEC_PER_SEC;
    nsec %= (int64_t)LINUX_NSEC_PER_SEC;
  } else if (nsec < 0) {
    int64_t borrow = (-nsec + (int64_t)LINUX_NSEC_PER_SEC - 1) /
                     (int64_t)LINUX_NSEC_PER_SEC;
    sec  -= borrow;
    nsec += borrow * (int64_t)LINUX_NSEC_PER_SEC;
  }

  out->tv_sec  = sec;
  out->tv_nsec = nsec;
  return 0;
}

/* ------------------------------------------------------------------ */
/* Boot-time setters.                                                 */
/* ------------------------------------------------------------------ */

void linux_clock_install_timebase(linux_clock_cycles_fn cycles,
                                  linux_clock_hz_fn hz,
                                  uint64_t tsc_start) {
  g_cycles_fn = cycles;
  g_hz_fn     = hz;
  g_tsc_start = tsc_start;
}

void linux_clock_install_wall_epoch(int64_t wall_seconds_at_boot,
                                    int64_t wall_nanoseconds_at_boot) {
  g_wall_seconds     = wall_seconds_at_boot;
  g_wall_nanoseconds = wall_nanoseconds_at_boot;
  g_wall_installed   = 1;
}

void linux_clock_reset_for_tests(void) {
  g_cycles_fn        = NULL;
  g_hz_fn            = NULL;
  g_tsc_start        = 0;
  g_wall_seconds     = 0;
  g_wall_nanoseconds = 0;
  g_wall_installed   = 0;
}

/* ------------------------------------------------------------------ */
/* Public dispatcher.                                                 */
/* ------------------------------------------------------------------ */

static int compute_monotonic(struct linux_timespec *out) {
  if (!g_cycles_fn || !g_hz_fn) {
    /* Timebase not yet installed: report success with t=0 so that
     * very-early-boot callers (before x64_timebase_init) do not
     * crash on a missing source. This mirrors how the Linux vdso
     * also returns t=0 if called pre-init via /proc/sys patches. */
    out->tv_sec  = 0;
    out->tv_nsec = 0;
    return 0;
  }

  uint64_t now = g_cycles_fn();
  uint64_t hz  = g_hz_fn();

  /* TSC is monotonic and start <= now is invariant. If a buggy
   * timebase reports otherwise, clamp to zero rather than crash. */
  uint64_t elapsed = (now > g_tsc_start) ? (now - g_tsc_start) : 0;
  return linux_clock_compute_timespec(elapsed, hz, out);
}

static int timebase_installed(void) {
    return g_cycles_fn && g_hz_fn;
}

int linux_clock_gettime(linux_clockid_t clk, struct linux_timespec *out) {
  if (!out) return -LINUX_EFAULT;

  switch (clk) {
    case LINUX_CLOCK_MONOTONIC:
    case LINUX_CLOCK_MONOTONIC_RAW:
    case LINUX_CLOCK_MONOTONIC_COARSE:
    case LINUX_CLOCK_BOOTTIME:
      return compute_monotonic(out);

    case LINUX_CLOCK_REALTIME:
    case LINUX_CLOCK_REALTIME_COARSE: {
      struct linux_timespec mono;
      int rc = compute_monotonic(&mono);
      if (rc != 0) return rc;
      if (!g_wall_installed) {
        /* No wall epoch known: report monotonic as wall. Firefox
         * tolerates this in single-process headless mode. */
        *out = mono;
        return 0;
      }
      struct linux_timespec wall;
      wall.tv_sec  = g_wall_seconds;
      wall.tv_nsec = g_wall_nanoseconds;
      return linux_clock_add_timespec(&wall, &mono, out);
    }

    case LINUX_CLOCK_PROCESS_CPUTIME_ID:
    case LINUX_CLOCK_THREAD_CPUTIME_ID:
      /* Per-thread/process accounting requires per-task accumulators
       * that S5 (pthread userland) and a future scheduler hook will
       * deliver. Until then, return ENOSYS so the caller can fall
       * back to CLOCK_MONOTONIC; Firefox's profiler already does. */
      return -LINUX_ENOSYS;

    default:
      return -LINUX_EINVAL;
  }
}

/* ------------------------------------------------------------------ */
/* Syscall adapters: unpack args + write back to userland pointer.    */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* musl bring-up: gettimeofday + nanosleep (sessao 20).               */
/* ------------------------------------------------------------------ */

int64_t linux_gettimeofday(struct linux_timeval *tv) {
    if (!tv) return -LINUX_EFAULT;
    struct linux_timespec ts;
    int rc = linux_clock_gettime(LINUX_CLOCK_REALTIME, &ts);
    if (rc != 0) {
        /* Fall back to MONOTONIC if REALTIME has no wall-clock
         * epoch installed -- this is what Linux does on early
         * boot before NTP/RTC reads, and what userland tolerates. */
        rc = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &ts);
        if (rc != 0) return (int64_t)rc;
    }
    tv->tv_sec  = (int64_t)ts.tv_sec;
    /* Truncate ns to us (Linux behaviour: just nanoseconds / 1000). */
    tv->tv_usec = (int64_t)(ts.tv_nsec / 1000);
    return 0;
}

/* Validate sec/nsec are non-negative and nsec < 1e9. */
static int valid_timespec(const struct linux_timespec *ts) {
    if (!ts) return 0;
    if (ts->tv_sec < 0) return 0;
    if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) return 0;
    return 1;
}

int64_t linux_nanosleep(const struct linux_timespec *req,
                        struct linux_timespec *rem) {
    if (!req) return -LINUX_EFAULT;
    if (!valid_timespec(req)) return -LINUX_EINVAL;
    if (!timebase_installed()) {
        if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
        return 0;
    }

    /* Compute the wake target on MONOTONIC timeline. */
    struct linux_timespec now;
    if (linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &now) != 0) {
        /* No timebase yet -- there's nothing to wait against, so
         * fast-return without doing anything. Marco M1 boot
         * sequence wires the timebase before userland runs, so
         * production callers never hit this. */
        if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
        return 0;
    }

    int64_t deadline_sec  = (int64_t)now.tv_sec  + req->tv_sec;
    int64_t deadline_nsec = (int64_t)now.tv_nsec + req->tv_nsec;
    if (deadline_nsec >= 1000000000) {
        deadline_sec  += 1;
        deadline_nsec -= 1000000000;
    }

    /* Spin until the deadline. The MONOTONIC clock advances based
     * on TSC, so this is a tight loop on the hot CPU. Future work
     * replaces this with `task_sleep_until(deadline)` when the
     * scheduler exposes that primitive. */
    for (;;) {
        if (linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &now) != 0) break;
        if ((int64_t)now.tv_sec  >  deadline_sec) break;
        if ((int64_t)now.tv_sec  == deadline_sec &&
            (int64_t)now.tv_nsec >= deadline_nsec) break;
    }

    /* On non-interrupted return, Linux convention is to leave rem
     * untouched OR zero it; we zero it for predictability. */
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

int64_t linux_clock_nanosleep(int32_t clockid, int flags,
                              const struct linux_timespec *req,
                              struct linux_timespec *rem) {
    if (!req) return -LINUX_EFAULT;
    if (!valid_timespec(req)) return -LINUX_EINVAL;
    if (flags & ~LINUX_TIMER_ABSTIME) return -LINUX_EINVAL;

    if (clockid == LINUX_CLOCK_PROCESS_CPUTIME_ID ||
        clockid == LINUX_CLOCK_THREAD_CPUTIME_ID) {
        return -LINUX_EOPNOTSUPP;
    }
    if (clockid != LINUX_CLOCK_MONOTONIC &&
        clockid != LINUX_CLOCK_MONOTONIC_RAW &&
        clockid != LINUX_CLOCK_MONOTONIC_COARSE &&
        clockid != LINUX_CLOCK_BOOTTIME &&
        clockid != LINUX_CLOCK_REALTIME &&
        clockid != LINUX_CLOCK_REALTIME_COARSE) {
        return -LINUX_EINVAL;
    }
    if (!timebase_installed()) {
        if (rem && !(flags & LINUX_TIMER_ABSTIME)) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        return 0;
    }

    /* Compute the deadline on the chosen clock. */
    struct linux_timespec deadline = *req;
    if (!(flags & LINUX_TIMER_ABSTIME)) {
        struct linux_timespec now;
        if (linux_clock_gettime((linux_clockid_t)clockid, &now) != 0) {
            if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
            return 0;
        }
        deadline.tv_sec  = now.tv_sec  + req->tv_sec;
        deadline.tv_nsec = now.tv_nsec + req->tv_nsec;
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec  += 1;
            deadline.tv_nsec -= 1000000000;
        }
    }

    for (;;) {
        struct linux_timespec now;
        if (linux_clock_gettime((linux_clockid_t)clockid, &now) != 0) break;
        if ((int64_t)now.tv_sec  >  deadline.tv_sec) break;
        if ((int64_t)now.tv_sec  == deadline.tv_sec &&
            (int64_t)now.tv_nsec >= deadline.tv_nsec) break;
    }

    if (rem && !(flags & LINUX_TIMER_ABSTIME)) {
        rem->tv_sec = 0; rem->tv_nsec = 0;
    }
    return 0;
}

/* `clock_gettime(clockid_t, struct timespec *)`. The userland pointer
 * is treated verbatim today: there is no per-process page-table
 * sandbox yet between the dispatcher and the user buffer. When S1.x
 * grows a `copy_to_user` primitive (memo'd in
 * `firefox-port-platform-shim.md` riscos), this adapter swaps to it.
 * Until then we write directly through `(struct linux_timespec *)`. */
static int64_t linux_syscall_clock_gettime(const struct linux_syscall_args *a) {
    linux_clockid_t clk = (linux_clockid_t)(int32_t)a->a0;
    struct linux_timespec *out = (struct linux_timespec *)(uintptr_t)a->a1;
    int rc = linux_clock_gettime(clk, out);
    return (int64_t)rc;
}

static int64_t sys_gettimeofday(const struct linux_syscall_args *a) {
    /* Linux signature: gettimeofday(struct timeval *tv,
     *                                struct timezone *tz). We
     * ignore tz (always NULL on modern Linux). */
    return linux_gettimeofday((struct linux_timeval *)(uintptr_t)a->a0);
}

static int64_t sys_nanosleep(const struct linux_syscall_args *a) {
    return linux_nanosleep(
        (const struct linux_timespec *)(uintptr_t)a->a0,
        (struct linux_timespec *)(uintptr_t)a->a1);
}

static int64_t sys_clock_nanosleep(const struct linux_syscall_args *a) {
    return linux_clock_nanosleep(
        (int32_t)a->a0, (int)a->a1,
        (const struct linux_timespec *)(uintptr_t)a->a2,
        (struct linux_timespec *)(uintptr_t)a->a3);
}

void linux_clock_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_clock_gettime,
                                 linux_syscall_clock_gettime);
    (void)linux_syscall_register(LINUX_NR_gettimeofday, sys_gettimeofday);
    (void)linux_syscall_register(LINUX_NR_nanosleep,    sys_nanosleep);
    (void)linux_syscall_register(LINUX_NR_clock_nanosleep,
                                 sys_clock_nanosleep);
}
