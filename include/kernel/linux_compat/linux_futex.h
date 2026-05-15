#ifndef KERNEL_LINUX_COMPAT_LINUX_FUTEX_H
#define KERNEL_LINUX_COMPAT_LINUX_FUTEX_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `futex(2)` shim (S1.5).
 *
 * The futex syscall underpins all of glibc/musl's pthread mutex,
 * cond, rwlock, and barrier primitives. SpiderMonkey relies on
 * pthread mutex; without futex the JS shell deadlocks on its
 * first lock acquisition.
 *
 * Marco M1 surface (Linux uapi/linux/futex.h):
 *
 *   FUTEX_WAIT (0)              -- wait if *uaddr == val
 *   FUTEX_WAKE (1)              -- wake up to N waiters
 *   FUTEX_REQUEUE (3)           -- wake some, requeue rest
 *   FUTEX_WAIT_BITSET (9)       -- WAIT with bitmask
 *   FUTEX_WAKE_BITSET (10)      -- WAKE with bitmask
 *
 *   FUTEX_PRIVATE_FLAG (0x80)   -- process-local (skips SHM check)
 *   FUTEX_CLOCK_REALTIME (0x100)-- timeout in CLOCK_REALTIME
 *
 * Operations not implemented yet: CMP_REQUEUE, FD, WAKE_OP,
 * LOCK_PI, UNLOCK_PI, TRYLOCK_PI, WAIT_REQUEUE_PI,
 * CMP_REQUEUE_PI. Userspace falls back to slow paths when those
 * return -ENOSYS.
 *
 * Layering: pure logic + injected ops (atomic_load / block / wake).
 * Production wires to `task_block` / `task_unblock_channel` from
 * `kernel/task.h`. Host tests use a deterministic in-memory
 * waiter table.
 */

/* Linux op constants. The op argument is `op | flags`; mask to
 * get the bare op. */
#define LINUX_FUTEX_WAIT             0
#define LINUX_FUTEX_WAKE             1
#define LINUX_FUTEX_FD               2  /* deprecated, returns -ENOSYS */
#define LINUX_FUTEX_REQUEUE          3
#define LINUX_FUTEX_CMP_REQUEUE      4
#define LINUX_FUTEX_WAKE_OP          5
#define LINUX_FUTEX_LOCK_PI          6
#define LINUX_FUTEX_UNLOCK_PI        7
#define LINUX_FUTEX_TRYLOCK_PI       8
#define LINUX_FUTEX_WAIT_BITSET      9
#define LINUX_FUTEX_WAKE_BITSET      10

#define LINUX_FUTEX_PRIVATE_FLAG     0x80
#define LINUX_FUTEX_CLOCK_REALTIME   0x100

/* Mask used to extract the bare op from a flags-decorated argument. */
#define LINUX_FUTEX_OP_MASK          0x7Fu
/* Mask of known flag bits (not part of the op number). */
#define LINUX_FUTEX_FLAGS_KNOWN \
    (LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME)

/* Sentinel timeout: 0 means "no timeout, block forever". Linux
 * uses NULL for this; the shim canonicalises to 0 ns. */
#define LINUX_FUTEX_NO_TIMEOUT 0ull

/* Result codes the `block_on` callback can return. */
enum linux_futex_block_result {
    LINUX_FUTEX_BLOCK_WOKEN     = 0,  /* normal wakeup */
    LINUX_FUTEX_BLOCK_TIMEDOUT  = 1,
    LINUX_FUTEX_BLOCK_INTR      = 2,  /* signal interrupted */
};

/* VMM/scheduler callback bundle. */
struct linux_futex_ops {
    /* Atomically read 32-bit value from `uaddr`. Returns 0 on
     * success, -1 on EFAULT (uaddr unreadable). */
    int (*atomic_load_u32)(const uint32_t *uaddr, uint32_t *out);

    /* Block the current task on `uaddr` (the address acts as the
     * wait channel). `timeout_ns == 0` means infinite wait.
     * Returns one of `linux_futex_block_result`. */
    int (*block_on)(const uint32_t *uaddr, uint64_t timeout_ns);

    /* Wake up to `max_waiters` blocked on `uaddr`. Returns the
     * number actually woken (0..max_waiters). */
    int (*wake)(const uint32_t *uaddr, int max_waiters);
};

void linux_futex_install_ops(const struct linux_futex_ops *ops);
void linux_futex_reset_for_tests(void);

/* Core futex entry. Returns:
 *   FUTEX_WAIT[_BITSET]  : 0 on wakeup, -EAGAIN if *uaddr != val,
 *                          -ETIMEDOUT, -EINTR.
 *   FUTEX_WAKE[_BITSET]  : number of waiters woken.
 *   FUTEX_REQUEUE        : number of waiters woken (val).
 *   Other ops            : -ENOSYS.
 *   uaddr alignment/null : -EFAULT.
 *   Unknown flag bit     : -EINVAL.
 *   No ops installed     : -ENOSYS.
 */
int64_t linux_futex(uint32_t *uaddr, int op, uint32_t val,
                    uint64_t timeout_ns,
                    uint32_t *uaddr2, uint32_t val3);

void linux_futex_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FUTEX_H */
