#include "kernel/linux_compat/linux_sched_prio.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Marco M1: single-task model. We track the "current task's"
 * policy + priority in module-local state. This survives
 * setscheduler/getscheduler round-trips even though the
 * scheduler doesn't honour the values. Per-task migration
 * happens when clone with thread groups lands. */
static int g_policy = LINUX_SCHED_OTHER;
static int g_priority = 0;

void linux_sched_prio_reset_for_tests(void) {
    g_policy = LINUX_SCHED_OTHER;
    g_priority = 0;
}

int linux_sched_policy_known(int policy) {
    switch (policy) {
        case LINUX_SCHED_OTHER:
        case LINUX_SCHED_FIFO:
        case LINUX_SCHED_RR:
        case LINUX_SCHED_BATCH:
        case LINUX_SCHED_IDLE:
        case LINUX_SCHED_DEADLINE:
            return 1;
        default:
            return 0;
    }
}

int linux_sched_priority_valid(int policy, int prio) {
    if (policy == LINUX_SCHED_FIFO || policy == LINUX_SCHED_RR) {
        return prio >= LINUX_SCHED_RT_MIN_PRIO &&
               prio <= LINUX_SCHED_RT_MAX_PRIO;
    }
    /* OTHER/BATCH/IDLE/DEADLINE: priority must be 0. */
    return prio == 0;
}

int64_t linux_sched_get_priority_max(int policy) {
    if (!linux_sched_policy_known(policy)) return -LINUX_EINVAL;
    if (policy == LINUX_SCHED_FIFO || policy == LINUX_SCHED_RR) {
        return LINUX_SCHED_RT_MAX_PRIO;
    }
    return 0;
}

int64_t linux_sched_get_priority_min(int policy) {
    if (!linux_sched_policy_known(policy)) return -LINUX_EINVAL;
    if (policy == LINUX_SCHED_FIFO || policy == LINUX_SCHED_RR) {
        return LINUX_SCHED_RT_MIN_PRIO;
    }
    return 0;
}

/* Marco M1: pid==0 means "current". Other pids are still
 * accepted as targeting current (single-task world). When
 * task tables land, this validates against the pid table. */
static int target_is_self(int pid) {
    return pid == 0;
}

int64_t linux_sched_setscheduler(int pid, int policy,
                                 const struct linux_sched_param *param) {
    (void)pid;
    if (!linux_sched_policy_known(policy)) return -LINUX_EINVAL;
    if (!param) return -LINUX_EFAULT;
    if (!linux_sched_priority_valid(policy, param->sched_priority)) {
        return -LINUX_EINVAL;
    }
    /* Linux: requires CAP_SYS_NICE for FIFO/RR. Marco M1 root
     * has it implicitly. */
    g_policy = policy;
    g_priority = param->sched_priority;
    return 0;
}

int64_t linux_sched_getscheduler(int pid) {
    if (pid < 0) return -LINUX_EINVAL;
    if (!target_is_self(pid)) {
        /* pid != 0: in Marco M1 single-task there's no other
         * task. Linux returns -ESRCH for non-existent pids;
         * we accept it as a self alias. */
    }
    return g_policy;
}

int64_t linux_sched_setparam(int pid, const struct linux_sched_param *param) {
    (void)pid;
    if (!param) return -LINUX_EFAULT;
    if (!linux_sched_priority_valid(g_policy, param->sched_priority)) {
        return -LINUX_EINVAL;
    }
    g_priority = param->sched_priority;
    return 0;
}

int64_t linux_sched_getparam(int pid, struct linux_sched_param *param) {
    (void)pid;
    if (!param) return -LINUX_EFAULT;
    param->sched_priority = g_priority;
    return 0;
}

static int64_t sys_setscheduler(const struct linux_syscall_args *a) {
    return linux_sched_setscheduler((int)a->a0, (int)a->a1,
                                    (const struct linux_sched_param *)
                                        (uintptr_t)a->a2);
}
static int64_t sys_getscheduler(const struct linux_syscall_args *a) {
    return linux_sched_getscheduler((int)a->a0);
}
static int64_t sys_setparam(const struct linux_syscall_args *a) {
    return linux_sched_setparam((int)a->a0,
                                (const struct linux_sched_param *)
                                    (uintptr_t)a->a1);
}
static int64_t sys_getparam(const struct linux_syscall_args *a) {
    return linux_sched_getparam((int)a->a0,
                                (struct linux_sched_param *)
                                    (uintptr_t)a->a1);
}
static int64_t sys_prio_max(const struct linux_syscall_args *a) {
    return linux_sched_get_priority_max((int)a->a0);
}
static int64_t sys_prio_min(const struct linux_syscall_args *a) {
    return linux_sched_get_priority_min((int)a->a0);
}

void linux_sched_prio_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_sched_setscheduler, sys_setscheduler);
    (void)linux_syscall_register(LINUX_NR_sched_getscheduler, sys_getscheduler);
    (void)linux_syscall_register(LINUX_NR_sched_setparam,     sys_setparam);
    (void)linux_syscall_register(LINUX_NR_sched_getparam,     sys_getparam);
    (void)linux_syscall_register(LINUX_NR_sched_get_priority_max, sys_prio_max);
    (void)linux_syscall_register(LINUX_NR_sched_get_priority_min, sys_prio_min);
}
