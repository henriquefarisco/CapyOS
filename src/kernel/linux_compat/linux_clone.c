#include "kernel/linux_compat/linux_clone.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

void linux_clone_reset_for_tests(void) {
    /* No state. */
}

/* Validate the flag set. Returns:
 *    0 -- flags pattern is recognised and would land if backend existed
 *   -LINUX_EINVAL -- unknown bits or invalid combination
 */
static int validate_flags(uint64_t flags) {
    /* Strip the low 8 bits (CSIGNAL); they hold the exit signal,
     * not a flag. */
    uint64_t f = flags & ~(uint64_t)LINUX_CSIGNAL;
    if (f & ~(uint64_t)LINUX_CLONE_KNOWN_FLAGS) return -1;

    /* CLONE_THREAD requires CLONE_SIGHAND (Linux invariant). */
    if ((flags & LINUX_CLONE_THREAD) &&
        !(flags & LINUX_CLONE_SIGHAND)) return -1;
    /* CLONE_SIGHAND requires CLONE_VM (Linux invariant). */
    if ((flags & LINUX_CLONE_SIGHAND) &&
        !(flags & LINUX_CLONE_VM)) return -1;
    /* CLONE_PARENT_SETTID/CHILD_CLEARTID without CLONE_THREAD or
     * an explicit ptid pointer is fine; we leave that check to the
     * implementation when it exists. */
    return 0;
}

int64_t linux_clone(uint64_t flags, uint64_t child_stack,
                    uint64_t ptid_ptr, uint64_t ctid_ptr,
                    uint64_t tls) {
    (void)child_stack; (void)ptid_ptr; (void)ctid_ptr; (void)tls;

    if (validate_flags(flags) != 0) return -LINUX_EINVAL;

    /* Recognised pattern -- ENOSYS until task_clone_thread exists. */
    return -LINUX_ENOSYS;
}

int64_t linux_clone3(uint64_t args_ptr, size_t size) {
    if (args_ptr == 0) return -LINUX_EFAULT;
    if (size != LINUX_CLONE_ARGS_SIZE_VER0 &&
        size != LINUX_CLONE_ARGS_SIZE_VER1 &&
        size != LINUX_CLONE_ARGS_SIZE_VER2) {
        return -LINUX_EINVAL;
    }

    const struct linux_clone_args *a =
        (const struct linux_clone_args *)(uintptr_t)args_ptr;
    if (validate_flags(a->flags) != 0) return -LINUX_EINVAL;
    return -LINUX_ENOSYS;
}

int64_t linux_fork(void) {
    /* fork() == clone(SIGCHLD, NULL, ...). The flag pattern is
     * legal, but we have no AS-clone path yet. */
    return -LINUX_ENOSYS;
}

int64_t linux_vfork(void) {
    /* vfork() == clone(CLONE_VM | CLONE_VFORK | SIGCHLD, ...). */
    return -LINUX_ENOSYS;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_clone(const struct linux_syscall_args *a) {
    /* x86_64 ABI: rdi=flags, rsi=child_stack, rdx=ptid,
     * r10=ctid, r8=tls. */
    return linux_clone(a->a0, a->a1, a->a2, a->a3, a->a4);
}

static int64_t sys_clone3(const struct linux_syscall_args *a) {
    return linux_clone3(a->a0, (size_t)a->a1);
}

static int64_t sys_fork(const struct linux_syscall_args *a) {
    (void)a;
    return linux_fork();
}

static int64_t sys_vfork(const struct linux_syscall_args *a) {
    (void)a;
    return linux_vfork();
}

void linux_clone_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_clone,  sys_clone);
    (void)linux_syscall_register(LINUX_NR_clone3, sys_clone3);
    (void)linux_syscall_register(LINUX_NR_fork,   sys_fork);
    (void)linux_syscall_register(LINUX_NR_vfork,  sys_vfork);
}
