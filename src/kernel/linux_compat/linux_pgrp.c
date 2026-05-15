#include "kernel/linux_compat/linux_pgrp.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_pgrp_ops g_ops;
static int                   g_ops_installed;
static int32_t               g_pgid = 1;
static int32_t               g_sid  = 1;

void linux_pgrp_install_ops(const struct linux_pgrp_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_pgrp_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_pgrp_reset_for_tests(void) {
    g_ops = (struct linux_pgrp_ops){0};
    g_ops_installed = 0;
    g_pgid = 1;
    g_sid  = 1;
}

static int32_t self_pid(void) {
    if (g_ops_installed && g_ops.getpid) return g_ops.getpid();
    return 1;
}

int64_t linux_setpgid(int32_t pid, int32_t pgid) {
    if (pid  < 0) return -LINUX_EINVAL;
    if (pgid < 0) return -LINUX_EINVAL;

    int32_t me = self_pid();
    int32_t target = (pid == 0) ? me : pid;
    int32_t newp   = (pgid == 0) ? target : pgid;

    if (target != me) {
        /* Marco M1 has no notion of "child"; we can't move
         * other processes' pgids. Linux returns EPERM in that
         * case for non-children. */
        return -LINUX_EPERM;
    }
    g_pgid = newp;
    return 0;
}

int64_t linux_getpgid(int32_t pid) {
    int32_t me = self_pid();
    if (pid != 0 && pid != me) return -LINUX_ESRCH;
    return (int64_t)g_pgid;
}

int64_t linux_getpgrp(void) {
    return (int64_t)g_pgid;
}

int64_t linux_setsid(void) {
    /* Linux returns EPERM if the caller is already a process
     * group leader. We model "not a leader" so first call
     * succeeds; subsequent calls (already leader) -> EPERM.
     * After success, sid = pgid = self. */
    int32_t me = self_pid();
    if (g_pgid == me) return -LINUX_EPERM;
    g_pgid = me;
    g_sid  = me;
    return (int64_t)g_sid;
}

int64_t linux_getsid(int32_t pid) {
    int32_t me = self_pid();
    if (pid != 0 && pid != me) return -LINUX_ESRCH;
    return (int64_t)g_sid;
}

static int64_t sys_setpgid(const struct linux_syscall_args *a) {
    return linux_setpgid((int32_t)a->a0, (int32_t)a->a1);
}
static int64_t sys_getpgid(const struct linux_syscall_args *a) {
    return linux_getpgid((int32_t)a->a0);
}
static int64_t sys_getpgrp(const struct linux_syscall_args *a) {
    (void)a; return linux_getpgrp();
}
static int64_t sys_setsid(const struct linux_syscall_args *a) {
    (void)a; return linux_setsid();
}
static int64_t sys_getsid(const struct linux_syscall_args *a) {
    return linux_getsid((int32_t)a->a0);
}

void linux_pgrp_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_setpgid, sys_setpgid);
    (void)linux_syscall_register(LINUX_NR_getpgid, sys_getpgid);
    (void)linux_syscall_register(LINUX_NR_getpgrp, sys_getpgrp);
    (void)linux_syscall_register(LINUX_NR_setsid,  sys_setsid);
    (void)linux_syscall_register(LINUX_NR_getsid,  sys_getsid);
}
