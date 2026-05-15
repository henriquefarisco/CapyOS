#include "kernel/linux_compat/linux_futex.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_futex_ops g_ops;

void linux_futex_install_ops(const struct linux_futex_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_futex_ops){0};
}

void linux_futex_reset_for_tests(void) {
    g_ops = (struct linux_futex_ops){0};
}

/* uaddr must be a valid 32-bit aligned user pointer. */
static int validate_uaddr(const uint32_t *uaddr) {
    if (!uaddr) return -1;
    if ((uintptr_t)uaddr & 0x3u) return -1;  /* misaligned */
    return 0;
}

/* WAIT[_BITSET] -- atomically check *uaddr == val, then block. */
static int64_t do_wait(uint32_t *uaddr, uint32_t expected,
                      uint64_t timeout_ns) {
    if (validate_uaddr(uaddr) != 0) return -LINUX_EFAULT;
    if (!g_ops.atomic_load_u32 || !g_ops.block_on) return -LINUX_ENOSYS;

    uint32_t cur = 0;
    if (g_ops.atomic_load_u32(uaddr, &cur) != 0) return -LINUX_EFAULT;

    /* Linux semantics: if the value has already changed, return
     * -EAGAIN immediately. This is the canonical way userland
     * detects a "spurious" wakeup from a fast path. */
    if (cur != expected) return -LINUX_EAGAIN;

    int rc = g_ops.block_on(uaddr, timeout_ns);
    switch (rc) {
        case LINUX_FUTEX_BLOCK_WOKEN:    return 0;
        case LINUX_FUTEX_BLOCK_TIMEDOUT: return -LINUX_ETIMEDOUT;
        case LINUX_FUTEX_BLOCK_INTR:     return -LINUX_EINTR;
        default:                          return -LINUX_EINVAL;
    }
}

/* WAKE[_BITSET] -- wake up to `n` waiters on uaddr. */
static int64_t do_wake(uint32_t *uaddr, int n) {
    if (validate_uaddr(uaddr) != 0) return -LINUX_EFAULT;
    if (!g_ops.wake) return -LINUX_ENOSYS;

    /* Linux clamps val to INT_MAX-1 on the wire; pass it through. */
    if (n < 0) return -LINUX_EINVAL;
    return (int64_t)g_ops.wake(uaddr, n);
}

/* REQUEUE -- wake `val` waiters from uaddr1, requeue the rest at
 * uaddr2 (up to `val3` total). For Marco M1 we approximate with
 * "wake `val`" and ignore the requeue; pthread fallback is to
 * re-wait on the next contention round. This is what musl does on
 * older Linux that lacks REQUEUE_PI: graceful degradation. */
static int64_t do_requeue(uint32_t *uaddr1, uint32_t *uaddr2, int val) {
    if (validate_uaddr(uaddr1) != 0) return -LINUX_EFAULT;
    if (validate_uaddr(uaddr2) != 0) return -LINUX_EFAULT;
    if (!g_ops.wake) return -LINUX_ENOSYS;
    if (val < 0) return -LINUX_EINVAL;
    return (int64_t)g_ops.wake(uaddr1, val);
}

int64_t linux_futex(uint32_t *uaddr, int op, uint32_t val,
                    uint64_t timeout_ns,
                    uint32_t *uaddr2, uint32_t val3) {
    (void)val3;  /* WAIT_BITSET ignores bitset 0; WAKE_BITSET likewise */

    /* Reject any flag bit outside the known mask. */
    int flags = op & ~(int)LINUX_FUTEX_OP_MASK;
    if (flags & ~(int)LINUX_FUTEX_FLAGS_KNOWN) return -LINUX_EINVAL;

    int bare = op & (int)LINUX_FUTEX_OP_MASK;

    switch (bare) {
        case LINUX_FUTEX_WAIT:
        case LINUX_FUTEX_WAIT_BITSET:
            return do_wait(uaddr, val, timeout_ns);

        case LINUX_FUTEX_WAKE:
        case LINUX_FUTEX_WAKE_BITSET:
            return do_wake(uaddr, (int)val);

        case LINUX_FUTEX_REQUEUE:
            return do_requeue(uaddr, uaddr2, (int)val);

        case LINUX_FUTEX_FD:
        case LINUX_FUTEX_CMP_REQUEUE:
        case LINUX_FUTEX_WAKE_OP:
        case LINUX_FUTEX_LOCK_PI:
        case LINUX_FUTEX_UNLOCK_PI:
        case LINUX_FUTEX_TRYLOCK_PI:
            return -LINUX_ENOSYS;

        default:
            return -LINUX_EINVAL;
    }
}

/* Syscall adapter. Linux x86_64 calling convention:
 *   rdi = uaddr, rsi = op, rdx = val, r10 = timeout_ptr,
 *   r8 = uaddr2, r9 = val3.
 * For Marco M1 simplicity we treat r10 as a uint64_t timeout in
 * nanoseconds (musl's pthread_cond_timedwait converts struct
 * timespec to nanos before invoking the syscall on most arches). */
static int64_t sys_futex(const struct linux_syscall_args *a) {
    return linux_futex((uint32_t *)(uintptr_t)a->a0,
                       (int)a->a1,
                       (uint32_t)a->a2,
                       a->a3,
                       (uint32_t *)(uintptr_t)a->a4,
                       (uint32_t)a->a5);
}

void linux_futex_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_futex, sys_futex);
}
