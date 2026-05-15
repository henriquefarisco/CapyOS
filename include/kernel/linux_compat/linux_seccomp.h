#ifndef KERNEL_LINUX_COMPAT_LINUX_SECCOMP_H
#define KERNEL_LINUX_COMPAT_LINUX_SECCOMP_H

/* Linux ABI seccomp + BPF + ptrace syscalls.
 *
 *   int seccomp(unsigned int operation, unsigned int flags,
 *                void *args);
 *   int bpf    (int cmd, union bpf_attr *attr, unsigned int size);
 *   long ptrace(int request, pid_t pid,
 *                void *addr, void *data);
 *
 * Why this matters for the Firefox port:
 *   - Firefox content sandbox installs its seccomp BPF filter
 *     via seccomp(SECCOMP_SET_MODE_FILTER) before exec'ing
 *     content processes. -ENOSYS makes the sandbox layer
 *     fail-closed and refuse to spawn the renderer.
 *   - Chromium/Firefox sandbox setup uses bpf(BPF_PROG_LOAD)
 *     to compile the seccomp filter to BPF bytecode; -ENOSYS
 *     forces interpretation (slow).
 *   - Crash reporters use ptrace(PTRACE_ATTACH) to capture a
 *     core dump from the crashing renderer. -ENOSYS disables
 *     crash collection (renderers crash silently).
 *
 * Linux semantics (subset; what Firefox actually issues):
 *   - seccomp operations: STRICT/FILTER/GET_ACTION_AVAIL/
 *     GET_NOTIF_SIZES. STRICT puts the task into a kill-on-
 *     anything-but-allowed-syscall mode; FILTER installs a
 *     BPF program.
 *   - bpf commands: PROG_LOAD/MAP_CREATE/MAP_LOOKUP/...; we
 *     accept structurally and report -ENOSYS so userland can
 *     gracefully fall back.
 *   - ptrace requests: ATTACH/DETACH/CONT/PEEKTEXT/POKETEXT/
 *     GETREGS/SETREGS/SYSCALL/etc. Marco M1 single-task ->
 *     ATTACH on self is valid; foreign pid -> -ESRCH.
 *
 * Marco M1 has no real seccomp/BPF infrastructure yet; we
 * accept structurally so Firefox's "is the kernel modern
 * enough?" probe takes its happy path. */

#include <stdint.h>
#include <stddef.h>

/* seccomp operations (uapi/linux/seccomp.h). */
#define LINUX_SECCOMP_SET_MODE_STRICT     0
#define LINUX_SECCOMP_SET_MODE_FILTER     1
#define LINUX_SECCOMP_GET_ACTION_AVAIL    2
#define LINUX_SECCOMP_GET_NOTIF_SIZES     3

/* seccomp filter flags. */
#define LINUX_SECCOMP_FILTER_FLAG_TSYNC          (1u << 0)
#define LINUX_SECCOMP_FILTER_FLAG_LOG            (1u << 1)
#define LINUX_SECCOMP_FILTER_FLAG_SPEC_ALLOW     (1u << 2)
#define LINUX_SECCOMP_FILTER_FLAG_NEW_LISTENER   (1u << 3)
#define LINUX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH    (1u << 4)
#define LINUX_SECCOMP_FILTER_FLAG_WAIT_KILL      (1u << 5)
#define LINUX_SECCOMP_FILTER_KNOWN \
    (LINUX_SECCOMP_FILTER_FLAG_TSYNC | LINUX_SECCOMP_FILTER_FLAG_LOG | \
     LINUX_SECCOMP_FILTER_FLAG_SPEC_ALLOW | \
     LINUX_SECCOMP_FILTER_FLAG_NEW_LISTENER | \
     LINUX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH | \
     LINUX_SECCOMP_FILTER_FLAG_WAIT_KILL)

/* SECCOMP_RET_* actions (only the well-known ones we report
 * via GET_ACTION_AVAIL; the actual action evaluation happens
 * inside the BPF program). */
#define LINUX_SECCOMP_RET_KILL_PROCESS  0x80000000
#define LINUX_SECCOMP_RET_KILL_THREAD   0x00000000
#define LINUX_SECCOMP_RET_TRAP          0x00030000
#define LINUX_SECCOMP_RET_ERRNO         0x00050000
#define LINUX_SECCOMP_RET_USER_NOTIF    0x7FC00000
#define LINUX_SECCOMP_RET_TRACE         0x7FF00000
#define LINUX_SECCOMP_RET_LOG           0x7FFC0000
#define LINUX_SECCOMP_RET_ALLOW         0x7FFF0000

/* bpf commands (uapi/linux/bpf.h subset). */
#define LINUX_BPF_MAP_CREATE       0
#define LINUX_BPF_MAP_LOOKUP_ELEM  1
#define LINUX_BPF_MAP_UPDATE_ELEM  2
#define LINUX_BPF_MAP_DELETE_ELEM  3
#define LINUX_BPF_MAP_GET_NEXT_KEY 4
#define LINUX_BPF_PROG_LOAD        5
#define LINUX_BPF_OBJ_PIN          6
#define LINUX_BPF_OBJ_GET          7
#define LINUX_BPF_PROG_ATTACH      8
#define LINUX_BPF_PROG_DETACH      9
#define LINUX_BPF_CMD_MAX          40  /* approximation; we
                                          accept up to 40 */

/* ptrace requests (uapi/linux/ptrace.h subset). */
#define LINUX_PTRACE_TRACEME       0
#define LINUX_PTRACE_PEEKTEXT      1
#define LINUX_PTRACE_PEEKDATA      2
#define LINUX_PTRACE_PEEKUSER      3
#define LINUX_PTRACE_POKETEXT      4
#define LINUX_PTRACE_POKEDATA      5
#define LINUX_PTRACE_POKEUSER      6
#define LINUX_PTRACE_CONT          7
#define LINUX_PTRACE_KILL          8
#define LINUX_PTRACE_SINGLESTEP    9
#define LINUX_PTRACE_GETREGS       12
#define LINUX_PTRACE_SETREGS       13
#define LINUX_PTRACE_GETFPREGS     14
#define LINUX_PTRACE_SETFPREGS     15
#define LINUX_PTRACE_ATTACH        16
#define LINUX_PTRACE_DETACH        17
#define LINUX_PTRACE_SYSCALL       24

struct linux_seccomp_ops {
    /* Optional callback for SECCOMP_SET_MODE_FILTER. NULL =
     * accept and discard (Marco M1 default). The callback
     * receives the BPF program pointer and length. */
    int64_t (*install_filter)(uint32_t flags,
                              const void *bpf_prog, size_t prog_len);
};

void linux_seccomp_install_ops(const struct linux_seccomp_ops *ops);
void linux_seccomp_reset_for_tests(void);

int64_t linux_seccomp(uint32_t operation, uint32_t flags, void *args);
int64_t linux_bpf    (int cmd, void *attr, uint32_t size);
int64_t linux_ptrace (int request, int pid, void *addr, void *data);

void linux_seccomp_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SECCOMP_H */
