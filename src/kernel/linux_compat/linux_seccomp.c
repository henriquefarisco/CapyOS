#include "kernel/linux_compat/linux_seccomp.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_seccomp_ops g_ops;
static int                      g_ops_installed;

void linux_seccomp_install_ops(const struct linux_seccomp_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_seccomp_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_seccomp_reset_for_tests(void) {
    g_ops = (struct linux_seccomp_ops){0};
    g_ops_installed = 0;
}

int64_t linux_seccomp(uint32_t operation, uint32_t flags, void *args) {
    switch (operation) {
        case LINUX_SECCOMP_SET_MODE_STRICT:
            /* STRICT mode: flags and args must be 0/NULL. */
            if (flags != 0) return -LINUX_EINVAL;
            if (args) return -LINUX_EINVAL;
            return 0;
        case LINUX_SECCOMP_SET_MODE_FILTER:
            if (flags & ~LINUX_SECCOMP_FILTER_KNOWN) return -LINUX_EINVAL;
            if (!args) return -LINUX_EFAULT;
            if (g_ops_installed && g_ops.install_filter) {
                /* args points to a struct sock_fprog with len + filter
                 * pointer; we forward the pointer + a "we don't know
                 * the actual length" sentinel. Real wiring lands when
                 * BPF interpreter does. */
                return g_ops.install_filter(flags, args, 0);
            }
            return 0;
        case LINUX_SECCOMP_GET_ACTION_AVAIL:
            /* args points to a u32 action; check whether it's
             * recognised. Linux returns 0 for known actions. */
            if (!args) return -LINUX_EFAULT;
            {
                uint32_t a = *(const uint32_t *)args;
                switch (a) {
                    case LINUX_SECCOMP_RET_KILL_PROCESS:
                    case LINUX_SECCOMP_RET_KILL_THREAD:
                    case LINUX_SECCOMP_RET_TRAP:
                    case LINUX_SECCOMP_RET_ERRNO:
                    case LINUX_SECCOMP_RET_USER_NOTIF:
                    case LINUX_SECCOMP_RET_TRACE:
                    case LINUX_SECCOMP_RET_LOG:
                    case LINUX_SECCOMP_RET_ALLOW:
                        return 0;
                    default:
                        return -LINUX_EOPNOTSUPP;
                }
            }
        case LINUX_SECCOMP_GET_NOTIF_SIZES:
            /* args points to struct seccomp_notif_sizes (6 bytes:
             * 3x u16). Linux fills it with the runtime sizes of
             * struct seccomp_notif and seccomp_notif_resp. */
            if (!args) return -LINUX_EFAULT;
            {
                uint16_t *sizes = (uint16_t *)args;
                /* Linux x86_64 sizes: notif=80, resp=24, data=16. */
                sizes[0] = 80;  /* seccomp_notif */
                sizes[1] = 24;  /* seccomp_notif_resp */
                sizes[2] = 16;  /* seccomp_data */
                return 0;
            }
        default:
            return -LINUX_EINVAL;
    }
}

int64_t linux_bpf(int cmd, void *attr, uint32_t size) {
    if (cmd < 0 || cmd >= LINUX_BPF_CMD_MAX) return -LINUX_EINVAL;
    if (!attr && size > 0) return -LINUX_EFAULT;
    /* Marco M1: no real BPF subsystem. Userland (libbpf, Firefox
     * sandbox compiler) probes for support and falls back to
     * interpreted classic BPF on -ENOSYS. */
    return -LINUX_ENOSYS;
}

int64_t linux_ptrace(int request, int pid, void *addr, void *data) {
    (void)addr; (void)data;
    if (request < 0) return -LINUX_EINVAL;
    /* TRACEME doesn't take a pid argument; everything else does. */
    if (request == LINUX_PTRACE_TRACEME) {
        /* Linux: "this task wants to be ptraced". Marco M1 single-
         * thread world: accept structurally; actual tracing requires
         * a parent task to attach. */
        return 0;
    }
    if (pid < 0) return -LINUX_EINVAL;
    /* All non-TRACEME requests target a peer. Marco M1 single-task:
     * pid==0 or pid==1 (init) -> -ESRCH; debuggers know to retry
     * after a clone. */
    if (pid != 0) return -LINUX_ESRCH;
    /* Self-targeting (pid == 0) is a Linux corner case the kernel
     * actually rejects with EPERM (you can't ptrace yourself).
     * Mirror that for a clean userland path. */
    return -LINUX_EPERM;
}

static int64_t sys_seccomp(const struct linux_syscall_args *a) {
    return linux_seccomp((uint32_t)a->a0, (uint32_t)a->a1,
                         (void *)(uintptr_t)a->a2);
}
static int64_t sys_bpf(const struct linux_syscall_args *a) {
    return linux_bpf((int)a->a0, (void *)(uintptr_t)a->a1,
                     (uint32_t)a->a2);
}
static int64_t sys_ptrace(const struct linux_syscall_args *a) {
    return linux_ptrace((int)a->a0, (int)a->a1,
                        (void *)(uintptr_t)a->a2,
                        (void *)(uintptr_t)a->a3);
}

void linux_seccomp_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_seccomp, sys_seccomp);
    (void)linux_syscall_register(LINUX_NR_bpf,     sys_bpf);
    (void)linux_syscall_register(LINUX_NR_ptrace,  sys_ptrace);
}
