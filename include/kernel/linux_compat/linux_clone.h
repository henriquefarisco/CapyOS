#ifndef KERNEL_LINUX_COMPAT_LINUX_CLONE_H
#define KERNEL_LINUX_COMPAT_LINUX_CLONE_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI clone/fork/clone3 shim (S1.4).
 *
 * `clone(2)` is the building block of pthread_create on Linux.
 * musl's pthread_create issues:
 *
 *   clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
 *         CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|
 *         CLONE_CHILD_CLEARTID,
 *         child_stack, ptid, tls, ctid)
 *
 * For Marco M1, CapyOS does not yet have multi-threaded processes
 * (one task = one process). Real implementation requires:
 *   - shared address space across tasks (CLONE_VM)
 *   - shared fd table (CLONE_FILES)
 *   - shared signal handlers (CLONE_SIGHAND)
 *   - thread group (CLONE_THREAD): same tgid for parent + child
 *
 * The shim therefore provides:
 *   1. Strict flag whitelist (rejects unknown bits with -EINVAL).
 *   2. Recognition of the canonical pthread_create flag set.
 *   3. -ENOSYS until `task_clone_thread` lands in the scheduler.
 *
 * This contract lets musl/SpiderMonkey detect "no real threads,
 * fall back to single-threaded paths" deterministically. Without
 * the contract userland would see -EINVAL, abort and abort the
 * whole process.
 *
 * fork() and vfork() are also stubbed here for symmetry with
 * -ENOSYS. They cannot legitimately succeed without CLONE_VM
 * negation (which requires AS clone). Userland tools (e.g.
 * Chromium IPC's content-process spawn) detect this and use
 * Mozilla's launch path instead.
 */

/* Linux clone flags (uapi/linux/sched.h). */
#define LINUX_CSIGNAL              0x000000FFu  /* signal mask in low 8 bits */
#define LINUX_CLONE_VM             0x00000100u
#define LINUX_CLONE_FS             0x00000200u
#define LINUX_CLONE_FILES          0x00000400u
#define LINUX_CLONE_SIGHAND        0x00000800u
#define LINUX_CLONE_PIDFD          0x00001000u
#define LINUX_CLONE_PTRACE         0x00002000u
#define LINUX_CLONE_VFORK          0x00004000u
#define LINUX_CLONE_PARENT         0x00008000u
#define LINUX_CLONE_THREAD         0x00010000u
#define LINUX_CLONE_NEWNS          0x00020000u
#define LINUX_CLONE_SYSVSEM        0x00040000u
#define LINUX_CLONE_SETTLS         0x00080000u
#define LINUX_CLONE_PARENT_SETTID  0x00100000u
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000u
#define LINUX_CLONE_DETACHED       0x00400000u
#define LINUX_CLONE_UNTRACED       0x00800000u
#define LINUX_CLONE_CHILD_SETTID   0x01000000u
#define LINUX_CLONE_NEWCGROUP      0x02000000u
#define LINUX_CLONE_NEWUTS         0x04000000u
#define LINUX_CLONE_NEWIPC         0x08000000u
#define LINUX_CLONE_NEWUSER        0x10000000u
#define LINUX_CLONE_NEWPID         0x20000000u
#define LINUX_CLONE_NEWNET         0x40000000u
#define LINUX_CLONE_IO             0x80000000u

/* The set of flags we recognise. Anything outside this mask
 * returns -EINVAL. (Linux mainline accepts more, but Marco M1
 * keeps the contract tight.) */
#define LINUX_CLONE_KNOWN_FLAGS \
    (LINUX_CSIGNAL | LINUX_CLONE_VM | LINUX_CLONE_FS | \
     LINUX_CLONE_FILES | LINUX_CLONE_SIGHAND | LINUX_CLONE_PIDFD | \
     LINUX_CLONE_VFORK | LINUX_CLONE_THREAD | LINUX_CLONE_SYSVSEM | \
     LINUX_CLONE_SETTLS | LINUX_CLONE_PARENT_SETTID | \
     LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_CHILD_SETTID | \
     LINUX_CLONE_DETACHED | LINUX_CLONE_UNTRACED)

/* The canonical "musl pthread_create" flag set. Userland can
 * test for this exact pattern; detecting any other indicates a
 * non-pthread caller (e.g. someone invoking syscall directly). */
#define LINUX_CLONE_PTHREAD_FLAGS \
    (LINUX_CLONE_VM | LINUX_CLONE_FS | LINUX_CLONE_FILES | \
     LINUX_CLONE_SIGHAND | LINUX_CLONE_THREAD | LINUX_CLONE_SYSVSEM | \
     LINUX_CLONE_SETTLS | LINUX_CLONE_PARENT_SETTID | \
     LINUX_CLONE_CHILD_CLEARTID)

/* Linux struct clone_args (uapi/linux/sched.h). Used by clone3. */
struct linux_clone_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
};

#define LINUX_CLONE_ARGS_SIZE_VER0 64u   /* size for kernel 5.3 */
#define LINUX_CLONE_ARGS_SIZE_VER1 80u   /* set_tid added in 5.5 */
#define LINUX_CLONE_ARGS_SIZE_VER2 88u   /* cgroup added in 5.7 */

void linux_clone_reset_for_tests(void);

/* clone(flags, child_stack, ptid, ctid, tls).
 *
 * Returns:
 *   -LINUX_EINVAL  if flags has unknown bits or the pattern is
 *                  unsupported.
 *   -LINUX_ENOSYS  if the pattern is recognised (e.g. pthread)
 *                  but the underlying scheduler primitive does
 *                  not exist yet.
 *
 * Successful return is "child gets 0, parent gets child tid"
 * but neither happens today -- always -ENOSYS for valid input.
 *
 * Note: Linux x86_64 clone signature is
 * `(flags, child_stack, ptid, ctid, tls)`. Some libc invoke a
 * legacy 4-arg form; the 5-arg form is canonical since 2.6.
 */
int64_t linux_clone(uint64_t flags, uint64_t child_stack,
                    uint64_t ptid_ptr, uint64_t ctid_ptr,
                    uint64_t tls);

/* clone3(args_ptr, size). args is a `struct linux_clone_args`. */
int64_t linux_clone3(uint64_t args_ptr, size_t size);

/* fork() / vfork() -- equivalent to specific clone() patterns. */
int64_t linux_fork(void);
int64_t linux_vfork(void);

void linux_clone_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CLONE_H */
