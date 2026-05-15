#include "kernel/linux_compat/linux_priority.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>

static int g_nice = 0;  /* default Linux nice */

void linux_priority_reset_for_tests(void) {
    g_nice = 0;
}

static int which_known(int which) {
    return which == LINUX_PRIO_PROCESS ||
           which == LINUX_PRIO_PGRP    ||
           which == LINUX_PRIO_USER;
}

int64_t linux_getpriority(int which, int who) {
    if (!which_known(which)) return -LINUX_EINVAL;
    /* Linux's special "encoded" return: subtract from 20 so
     * the legal range is 1..40 (nice +19..-20). Userland that
     * handles -1 specially clears errno before the call. */
    int encoded = 20 - g_nice;
    (void)who; /* Marco M1 has only one task / group / user. */
    return (int64_t)encoded;
}

int64_t linux_setpriority(int which, int who, int prio) {
    if (!which_known(which)) return -LINUX_EINVAL;
    /* Linux clamps prio into [NICE_MIN, NICE_MAX]; values
     * outside that range are silently brought into bounds. */
    if (prio < LINUX_NICE_MIN) prio = LINUX_NICE_MIN;
    if (prio > LINUX_NICE_MAX) prio = LINUX_NICE_MAX;
    g_nice = prio;
    (void)who;
    return 0;
}

static int64_t sys_getpriority(const struct linux_syscall_args *a) {
    return linux_getpriority((int)a->a0, (int)a->a1);
}

static int64_t sys_setpriority(const struct linux_syscall_args *a) {
    return linux_setpriority((int)a->a0, (int)a->a1, (int)a->a2);
}

void linux_priority_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getpriority, sys_getpriority);
    (void)linux_syscall_register(LINUX_NR_setpriority, sys_setpriority);
}
