#ifndef KERNEL_LINUX_COMPAT_LINUX_PROC_VM_H
#define KERNEL_LINUX_COMPAT_LINUX_PROC_VM_H

/* Linux ABI process-VM + kcmp syscalls.
 *
 *   ssize_t process_vm_readv (pid_t pid,
 *                             const struct iovec *local_iov,
 *                             unsigned long liovcnt,
 *                             const struct iovec *remote_iov,
 *                             unsigned long riovcnt,
 *                             unsigned long flags);
 *   ssize_t process_vm_writev(pid_t pid, ...same shape...);
 *   int     kcmp            (pid_t pid1, pid_t pid2, int type,
 *                             unsigned long idx1, unsigned long idx2);
 *
 * Why this matters for the Firefox port:
 *   - Firefox profiler reads other thread stacks via
 *     process_vm_readv to capture sample frames without
 *     pausing the target. -ENOSYS forces the profiler to fall
 *     back to ptrace(PTRACE_GETREGS), which is much slower
 *     and observable from the targeted thread.
 *   - Chromium-derived sandboxes (used by Firefox content
 *     processes) call kcmp(KCMP_FILE) to detect whether two
 *     fds refer to the same kernel object before granting
 *     IPC permissions; -ENOSYS makes them fall-closed and
 *     refuse the IPC.
 *   - GDB-style debuggers use process_vm_writev to inject
 *     breakpoint instructions; -ENOSYS forces them onto
 *     ptrace(PTRACE_POKETEXT) (slower).
 *
 * Linux semantics:
 *   - process_vm_readv: pid 0 means "self"; flags must be 0;
 *     liovcnt/riovcnt > IOV_MAX (1024) -> -EINVAL.
 *   - process_vm_writev: same, requires CAP_SYS_PTRACE for
 *     non-self peers.
 *   - kcmp: pid1/pid2 must be valid; type whitelist (FILE,
 *     VM, FILES, FS, SIGHAND, IO, SYSVSEM, EPOLL_TFD).
 *
 * Marco M1 single-task: pid != 0 (and != current.pid) ->
 * -ESRCH; self peer reads/writes are accepted via the
 * provider hook so userland's "is this even supported?"
 * probe takes its happy path. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_PROC_VM_IOV_MAX  1024

struct linux_proc_vm_iovec {
    void   *iov_base;
    size_t  iov_len;
};

/* kcmp(2) types from include/uapi/linux/kcmp.h. */
#define LINUX_KCMP_FILE        0
#define LINUX_KCMP_VM          1
#define LINUX_KCMP_FILES       2
#define LINUX_KCMP_FS          3
#define LINUX_KCMP_SIGHAND     4
#define LINUX_KCMP_IO          5
#define LINUX_KCMP_SYSVSEM     6
#define LINUX_KCMP_EPOLL_TFD   7
#define LINUX_KCMP_TYPES       8

struct linux_proc_vm_ops {
    /* Optional callback. NULL = self-only iov sum reported. */
    int64_t (*read_self)(const struct linux_proc_vm_iovec *local_iov,
                         size_t liovcnt,
                         const struct linux_proc_vm_iovec *remote_iov,
                         size_t riovcnt);
    int64_t (*write_self)(const struct linux_proc_vm_iovec *local_iov,
                          size_t liovcnt,
                          const struct linux_proc_vm_iovec *remote_iov,
                          size_t riovcnt);
    /* Returns the current task's pid; lets us detect self
     * peers without pulling in linux_process. */
    int (*current_pid)(void);
};

void linux_proc_vm_install_ops(const struct linux_proc_vm_ops *ops);
void linux_proc_vm_reset_for_tests(void);

int64_t linux_process_vm_readv (int pid,
                                const struct linux_proc_vm_iovec *local_iov,
                                size_t liovcnt,
                                const struct linux_proc_vm_iovec *remote_iov,
                                size_t riovcnt,
                                uint64_t flags);
int64_t linux_process_vm_writev(int pid,
                                const struct linux_proc_vm_iovec *local_iov,
                                size_t liovcnt,
                                const struct linux_proc_vm_iovec *remote_iov,
                                size_t riovcnt,
                                uint64_t flags);
int64_t linux_kcmp(int pid1, int pid2, int type,
                   uint64_t idx1, uint64_t idx2);

void linux_proc_vm_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PROC_VM_H */
