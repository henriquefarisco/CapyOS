#include "kernel/linux_compat/linux_proc_vm.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_proc_vm_ops g_ops;
static int                      g_ops_installed;

void linux_proc_vm_install_ops(const struct linux_proc_vm_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_proc_vm_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_proc_vm_reset_for_tests(void) {
    g_ops = (struct linux_proc_vm_ops){0};
    g_ops_installed = 0;
}

static int target_is_self(int pid) {
    if (pid == 0) return 1;
    if (g_ops_installed && g_ops.current_pid) {
        return pid == g_ops.current_pid();
    }
    /* No current_pid hook; treat any pid >0 as a foreign peer
     * so userland sees the right ESRCH path. */
    return 0;
}

static int64_t common_validate(int pid,
                               const struct linux_proc_vm_iovec *local,
                               size_t liovcnt,
                               const struct linux_proc_vm_iovec *remote,
                               size_t riovcnt,
                               uint64_t flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (pid < 0) return -LINUX_EINVAL;
    if (liovcnt > LINUX_PROC_VM_IOV_MAX) return -LINUX_EINVAL;
    if (riovcnt > LINUX_PROC_VM_IOV_MAX) return -LINUX_EINVAL;
    /* iovcnt == 0 is well-formed; no iov needed. */
    if ((liovcnt > 0 && !local) || (riovcnt > 0 && !remote)) {
        return -LINUX_EFAULT;
    }
    return 0;
}

int64_t linux_process_vm_readv(int pid,
                               const struct linux_proc_vm_iovec *local_iov,
                               size_t liovcnt,
                               const struct linux_proc_vm_iovec *remote_iov,
                               size_t riovcnt,
                               uint64_t flags) {
    int64_t rc = common_validate(pid, local_iov, liovcnt,
                                 remote_iov, riovcnt, flags);
    if (rc) return rc;
    if (!target_is_self(pid)) return -LINUX_ESRCH;
    if (g_ops_installed && g_ops.read_self) {
        return g_ops.read_self(local_iov, liovcnt,
                               remote_iov, riovcnt);
    }
    /* No backend: report 0 bytes (same as a successful but
     * empty read). Userland tolerates this and falls back. */
    return 0;
}

int64_t linux_process_vm_writev(int pid,
                                const struct linux_proc_vm_iovec *local_iov,
                                size_t liovcnt,
                                const struct linux_proc_vm_iovec *remote_iov,
                                size_t riovcnt,
                                uint64_t flags) {
    int64_t rc = common_validate(pid, local_iov, liovcnt,
                                 remote_iov, riovcnt, flags);
    if (rc) return rc;
    if (!target_is_self(pid)) return -LINUX_EPERM;
    if (g_ops_installed && g_ops.write_self) {
        return g_ops.write_self(local_iov, liovcnt,
                                remote_iov, riovcnt);
    }
    return 0;
}

int64_t linux_kcmp(int pid1, int pid2, int type,
                   uint64_t idx1, uint64_t idx2) {
    if (pid1 < 0 || pid2 < 0) return -LINUX_EINVAL;
    if (type < 0 || type >= LINUX_KCMP_TYPES) return -LINUX_EINVAL;
    /* Linux: idx args are only meaningful for KCMP_FILE and
     * KCMP_EPOLL_TFD; we accept them silently otherwise. */
    if (type == LINUX_KCMP_FILE) {
        /* idx1/idx2 are fds. Linux: invalid fd -> -EBADF. We
         * compare structurally: same fd value -> equal. */
        return (idx1 == idx2) ? 0 : 1;
    }
    /* For all other resource kinds in Marco M1 single-task,
     * comparing pid1 vs pid2: equal pids -> 0 (same), else 1. */
    return (pid1 == pid2) ? 0 : 1;
}

static int64_t sys_proc_vm_readv(const struct linux_syscall_args *a) {
    return linux_process_vm_readv((int)a->a0,
        (const struct linux_proc_vm_iovec *)(uintptr_t)a->a1,
        (size_t)a->a2,
        (const struct linux_proc_vm_iovec *)(uintptr_t)a->a3,
        (size_t)a->a4,
        (uint64_t)a->a5);
}
static int64_t sys_proc_vm_writev(const struct linux_syscall_args *a) {
    return linux_process_vm_writev((int)a->a0,
        (const struct linux_proc_vm_iovec *)(uintptr_t)a->a1,
        (size_t)a->a2,
        (const struct linux_proc_vm_iovec *)(uintptr_t)a->a3,
        (size_t)a->a4,
        (uint64_t)a->a5);
}
static int64_t sys_kcmp(const struct linux_syscall_args *a) {
    return linux_kcmp((int)a->a0, (int)a->a1, (int)a->a2,
                      (uint64_t)a->a3, (uint64_t)a->a4);
}

void linux_proc_vm_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_process_vm_readv,
                                 sys_proc_vm_readv);
    (void)linux_syscall_register(LINUX_NR_process_vm_writev,
                                 sys_proc_vm_writev);
    (void)linux_syscall_register(LINUX_NR_kcmp, sys_kcmp);
}
