#include "kernel/linux_compat/linux_kill.h"
#include "kernel/linux_compat/linux_signal.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_kill_ops g_ops;
static int                   g_ops_installed;

void linux_kill_install_ops(const struct linux_kill_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_kill_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_kill_reset_for_tests(void) {
    g_ops = (struct linux_kill_ops){0};
    g_ops_installed = 0;
}

static int sig_valid(int sig) {
    return sig >= 0 && (uint32_t)sig <= LINUX_NSIG;
}

static int32_t self_pid(void) {
    if (g_ops_installed && g_ops.getpid) return g_ops.getpid();
    return 1; /* Marco M1 default pid (single task). */
}

int64_t linux_kill(int32_t pid, int sig) {
    if (!sig_valid(sig)) return -LINUX_EINVAL;

    int32_t me = self_pid();

    if (pid == me) {
        /* Self-signal: delegate if a deliverer is installed,
         * otherwise no-op success. */
        if (sig == 0) return 0;
        if (g_ops_installed && g_ops.deliver) return g_ops.deliver(pid, sig);
        return 0;
    }
    if (pid == 0)  return 0;     /* signal own pgrp -> no peers */
    if (pid == -1) return 0;     /* broadcast -> no peers */
    if (pid <  -1) return -LINUX_ESRCH;
    /* pid > 0 and not self -> no such process. */
    return -LINUX_ESRCH;
}

int64_t linux_tgkill(int32_t tgid, int32_t tid, int sig) {
    if (tgid <= 0 || tid <= 0) return -LINUX_EINVAL;
    if (!sig_valid(sig))       return -LINUX_EINVAL;

    int32_t me = self_pid();
    if (tgid == me && tid == me) {
        if (sig == 0) return 0;
        if (g_ops_installed && g_ops.deliver) return g_ops.deliver(tid, sig);
        return 0;
    }
    return -LINUX_ESRCH;
}

int64_t linux_tkill(int32_t tid, int sig) {
    if (tid <= 0)        return -LINUX_EINVAL;
    if (!sig_valid(sig)) return -LINUX_EINVAL;

    int32_t me = self_pid();
    if (tid == me) {
        if (sig == 0) return 0;
        if (g_ops_installed && g_ops.deliver) return g_ops.deliver(tid, sig);
        return 0;
    }
    return -LINUX_ESRCH;
}

static int64_t sys_kill(const struct linux_syscall_args *a) {
    return linux_kill((int32_t)a->a0, (int)a->a1);
}

static int64_t sys_tgkill(const struct linux_syscall_args *a) {
    return linux_tgkill((int32_t)a->a0, (int32_t)a->a1, (int)a->a2);
}

static int64_t sys_tkill(const struct linux_syscall_args *a) {
    return linux_tkill((int32_t)a->a0, (int)a->a1);
}

void linux_kill_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_kill, sys_kill);
    (void)linux_syscall_register(LINUX_NR_tgkill, sys_tgkill);
    (void)linux_syscall_register(LINUX_NR_tkill, sys_tkill);
}
