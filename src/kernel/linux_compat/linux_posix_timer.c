#include "kernel/linux_compat/linux_posix_timer.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

struct timer_slot {
    int                       in_use;
    int                       clockid;
    int                       notify;     /* SIGEV_* */
    struct linux_itimerspec   spec;
};

static struct timer_slot g_timers[LINUX_POSIX_TIMER_MAX];

void linux_posix_timer_reset_for_tests(void) {
    for (int i = 0; i < LINUX_POSIX_TIMER_MAX; i++) {
        g_timers[i].in_use = 0;
        g_timers[i].clockid = 0;
        g_timers[i].notify = 0;
        g_timers[i].spec.it_interval.tv_sec = 0;
        g_timers[i].spec.it_interval.tv_nsec = 0;
        g_timers[i].spec.it_value.tv_sec = 0;
        g_timers[i].spec.it_value.tv_nsec = 0;
    }
}

static int clockid_supported(int clockid) {
    switch (clockid) {
        case LINUX_CLOCK_REALTIME:
        case LINUX_CLOCK_MONOTONIC:
        case LINUX_CLOCK_PROCESS_CPUTIME_ID:
        case LINUX_CLOCK_THREAD_CPUTIME_ID:
        case LINUX_CLOCK_MONOTONIC_RAW:
        case LINUX_CLOCK_REALTIME_COARSE:
        case LINUX_CLOCK_MONOTONIC_COARSE:
        case LINUX_CLOCK_BOOTTIME:
            return 1;
        default:
            return 0;
    }
}

static int notify_supported(int notify) {
    switch (notify) {
        case LINUX_SIGEV_SIGNAL:
        case LINUX_SIGEV_NONE:
        case LINUX_SIGEV_THREAD:
        case LINUX_SIGEV_THREAD_ID:
            return 1;
        default:
            return 0;
    }
}

static int timespec_valid(const struct linux_timespec_pt *ts) {
    if (ts->tv_sec < 0) return 0;
    if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) return 0;
    return 1;
}

static int alloc_slot(void) {
    for (int i = 0; i < LINUX_POSIX_TIMER_MAX; i++) {
        if (!g_timers[i].in_use) return i;
    }
    return -1;
}

static int valid_id(int id) {
    return id >= 1 && id <= LINUX_POSIX_TIMER_MAX &&
           g_timers[id - 1].in_use;
}

int64_t linux_timer_create(int clockid, void *sevp, int *timerid) {
    if (!timerid) return -LINUX_EFAULT;
    if (!clockid_supported(clockid)) return -LINUX_EINVAL;
    int notify = LINUX_SIGEV_SIGNAL; /* default */
    if (sevp) {
        const struct linux_sigevent_subset *s =
            (const struct linux_sigevent_subset *)sevp;
        if (!notify_supported(s->sigev_notify)) return -LINUX_EINVAL;
        notify = s->sigev_notify;
    }
    int slot = alloc_slot();
    if (slot < 0) return -LINUX_EAGAIN;
    g_timers[slot].in_use = 1;
    g_timers[slot].clockid = clockid;
    g_timers[slot].notify = notify;
    g_timers[slot].spec.it_interval.tv_sec = 0;
    g_timers[slot].spec.it_interval.tv_nsec = 0;
    g_timers[slot].spec.it_value.tv_sec = 0;
    g_timers[slot].spec.it_value.tv_nsec = 0;
    *timerid = slot + 1;  /* ids are 1-based */
    return 0;
}

int64_t linux_timer_settime(int timerid, int flags,
                            const struct linux_itimerspec *new_value,
                            struct linux_itimerspec *old_value) {
    if (!valid_id(timerid)) return -LINUX_EINVAL;
    if (!new_value) return -LINUX_EFAULT;
    if (!timespec_valid(&new_value->it_interval) ||
        !timespec_valid(&new_value->it_value)) {
        return -LINUX_EINVAL;
    }
    if (flags & ~LINUX_TIMER_ABSTIME) return -LINUX_EINVAL;
    int idx = timerid - 1;
    if (old_value) *old_value = g_timers[idx].spec;
    g_timers[idx].spec = *new_value;
    return 0;
}

int64_t linux_timer_gettime(int timerid,
                            struct linux_itimerspec *curr_value) {
    if (!valid_id(timerid)) return -LINUX_EINVAL;
    if (!curr_value) return -LINUX_EFAULT;
    *curr_value = g_timers[timerid - 1].spec;
    return 0;
}

int64_t linux_timer_getoverrun(int timerid) {
    if (!valid_id(timerid)) return -LINUX_EINVAL;
    /* No timer ever fires in Marco M1, so no overruns. */
    return 0;
}

int64_t linux_timer_delete(int timerid) {
    if (!valid_id(timerid)) return -LINUX_EINVAL;
    g_timers[timerid - 1].in_use = 0;
    return 0;
}

static int64_t sys_create(const struct linux_syscall_args *a) {
    return linux_timer_create((int)a->a0, (void *)(uintptr_t)a->a1,
                              (int *)(uintptr_t)a->a2);
}
static int64_t sys_settime(const struct linux_syscall_args *a) {
    return linux_timer_settime((int)a->a0, (int)a->a1,
                               (const struct linux_itimerspec *)
                                   (uintptr_t)a->a2,
                               (struct linux_itimerspec *)
                                   (uintptr_t)a->a3);
}
static int64_t sys_gettime(const struct linux_syscall_args *a) {
    return linux_timer_gettime((int)a->a0,
                               (struct linux_itimerspec *)
                                   (uintptr_t)a->a1);
}
static int64_t sys_getoverrun(const struct linux_syscall_args *a) {
    return linux_timer_getoverrun((int)a->a0);
}
static int64_t sys_delete(const struct linux_syscall_args *a) {
    return linux_timer_delete((int)a->a0);
}

void linux_posix_timer_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_timer_create,     sys_create);
    (void)linux_syscall_register(LINUX_NR_timer_settime,    sys_settime);
    (void)linux_syscall_register(LINUX_NR_timer_gettime,    sys_gettime);
    (void)linux_syscall_register(LINUX_NR_timer_getoverrun, sys_getoverrun);
    (void)linux_syscall_register(LINUX_NR_timer_delete,     sys_delete);
}
