#include "kernel/linux_compat/linux_itimer.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_itimer_ops g_ops;
static int                     g_ops_installed;
static uint32_t                g_alarm_seconds;
static struct linux_itimerval  g_itimers[LINUX_ITIMER_COUNT];

void linux_itimer_install_ops(const struct linux_itimer_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_itimer_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_itimer_reset_for_tests(void) {
    g_ops = (struct linux_itimer_ops){0};
    g_ops_installed = 0;
    g_alarm_seconds = 0;
    for (int i = 0; i < LINUX_ITIMER_COUNT; i++) {
        g_itimers[i].it_interval.tv_sec = 0;
        g_itimers[i].it_interval.tv_usec = 0;
        g_itimers[i].it_value.tv_sec = 0;
        g_itimers[i].it_value.tv_usec = 0;
    }
}

uint32_t linux_alarm(uint32_t seconds) {
    uint32_t prev = g_alarm_seconds;
    g_alarm_seconds = seconds;
    return prev;
}

static int valid_which(int which) {
    return which >= 0 && which < LINUX_ITIMER_COUNT;
}

static int valid_usec(int64_t us) {
    return us >= 0 && us < 1000000;
}

int64_t linux_getitimer(int which, struct linux_itimerval *curr_value) {
    if (!valid_which(which)) return -LINUX_EINVAL;
    if (!curr_value) return -LINUX_EFAULT;
    *curr_value = g_itimers[which];
    return 0;
}

int64_t linux_setitimer(int which,
                        const struct linux_itimerval *new_value,
                        struct linux_itimerval *old_value) {
    if (!valid_which(which)) return -LINUX_EINVAL;
    if (!new_value) return -LINUX_EFAULT;
    if (!valid_usec(new_value->it_interval.tv_usec) ||
        !valid_usec(new_value->it_value.tv_usec)) {
        return -LINUX_EINVAL;
    }
    if (new_value->it_interval.tv_sec < 0 ||
        new_value->it_value.tv_sec < 0) {
        return -LINUX_EINVAL;
    }
    if (old_value) *old_value = g_itimers[which];
    g_itimers[which] = *new_value;
    return 0;
}

int64_t linux_times(struct linux_tms *buf) {
    int64_t ticks = (g_ops_installed && g_ops.now_ticks)
                    ? g_ops.now_ticks() : 0;
    if (buf) {
        buf->tms_utime = 0;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return ticks;
}

static int64_t sys_alarm(const struct linux_syscall_args *a) {
    return (int64_t)linux_alarm((uint32_t)a->a0);
}
static int64_t sys_getitimer(const struct linux_syscall_args *a) {
    return linux_getitimer((int)a->a0,
                           (struct linux_itimerval *)(uintptr_t)a->a1);
}
static int64_t sys_setitimer(const struct linux_syscall_args *a) {
    return linux_setitimer((int)a->a0,
                           (const struct linux_itimerval *)(uintptr_t)a->a1,
                           (struct linux_itimerval *)(uintptr_t)a->a2);
}
static int64_t sys_times(const struct linux_syscall_args *a) {
    return linux_times((struct linux_tms *)(uintptr_t)a->a0);
}

void linux_itimer_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_alarm,     sys_alarm);
    (void)linux_syscall_register(LINUX_NR_getitimer, sys_getitimer);
    (void)linux_syscall_register(LINUX_NR_setitimer, sys_setitimer);
    (void)linux_syscall_register(LINUX_NR_times,     sys_times);
}
