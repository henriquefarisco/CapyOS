#include "kernel/linux_compat/linux_wait.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

int64_t linux_wait4(int32_t pid, int *wstatus,
                    int options, void *rusage) {
    (void)pid;
    (void)wstatus;
    (void)rusage;
    if (options & ~LINUX_WAIT_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* No children in Marco M1: faithful Linux answer. */
    return -LINUX_ECHILD;
}

int64_t linux_waitid(int idtype, int32_t id,
                     void *infop, int options) {
    (void)id;
    if (idtype != LINUX_P_ALL && idtype != LINUX_P_PID &&
        idtype != LINUX_P_PGID && idtype != LINUX_P_PIDFD) {
        return -LINUX_EINVAL;
    }
    /* waitid requires at least one of WEXITED/WSTOPPED/WCONTINUED
     * (Linux mandates this; without any of them, no event can
     * ever match and the call would block forever). */
    if ((options & (LINUX_WEXITED | LINUX_WSTOPPED |
                    LINUX_WCONTINUED)) == 0) {
        return -LINUX_EINVAL;
    }
    if (options & ~LINUX_WAIT_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* infop may be NULL only when WNOHANG and no event happened
     * (Linux documents NULL infop as legal for newer kernels);
     * we don't fault on it because Marco M1 has no events. */
    (void)infop;
    return -LINUX_ECHILD;
}

static int64_t sys_wait4(const struct linux_syscall_args *a) {
    return linux_wait4((int32_t)a->a0,
                       (int *)(uintptr_t)a->a1,
                       (int)a->a2,
                       (void *)(uintptr_t)a->a3);
}

static int64_t sys_waitid(const struct linux_syscall_args *a) {
    return linux_waitid((int)a->a0,
                        (int32_t)a->a1,
                        (void *)(uintptr_t)a->a2,
                        (int)a->a3);
}

void linux_wait_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_wait4, sys_wait4);
    (void)linux_syscall_register(LINUX_NR_waitid, sys_waitid);
}
