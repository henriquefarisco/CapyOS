#include "kernel/linux_compat/linux_futex.h"

/* Boot wiring for `linux_futex` against the real scheduler.
 * Excluded from host tests.
 *
 * Marco M1 strategy: use the kernel's existing
 * `task_block(t, channel)` + `task_unblock_channel(channel)` as
 * the primitive. The futex address `uaddr` is used directly as
 * the wait channel pointer.
 *
 * Limitations vs Linux mainline:
 *   - Timeout is currently ignored (the kernel scheduler does
 *     not yet expose a timed block primitive). Returns
 *     LINUX_FUTEX_BLOCK_WOKEN unconditionally on wakeup.
 *     SpiderMonkey pthread_cond_timedwait will appear to never
 *     time out; mitigated by surrounding application logic that
 *     also polls. Will be fixed when `task_sleep_until` lands.
 *   - The wake count returned is approximate: we wake all
 *     waiters on the channel even if `max_waiters` is 1
 *     (Linux semantics: at most N woken). For pthread mutex
 *     this is correct (always wake 1, only one in queue at a
 *     time). For cond broadcast it's also correct. For weird
 *     futex op patterns it may surface as spurious wakeups
 *     which musl handles via its retry loop.
 *   - Atomic load: a plain volatile read. CapyOS user pages do
 *     not page out today so EFAULT is impossible from kernel
 *     mode (kernel can read any user page that is mapped at
 *     the time of the call).
 */

#if !defined(UNIT_TEST)

#include "kernel/task.h"

#include <stdint.h>
#include <stddef.h>

static int wrap_atomic_load_u32(const uint32_t *uaddr, uint32_t *out) {
    if (!uaddr || !out) return -1;
    /* x86-64 32-bit aligned loads are atomic at the hardware
     * level, but volatile guarantees the compiler does not fuse
     * or reorder. */
    *out = *(const volatile uint32_t *)uaddr;
    return 0;
}

static int wrap_block_on(const uint32_t *uaddr, uint64_t timeout_ns) {
    (void)timeout_ns;  /* TODO: integrate with task_sleep_until */
    struct task *cur = task_current();
    if (!cur) return LINUX_FUTEX_BLOCK_WOKEN;
    /* `uaddr` is the wait channel: any task blocked on the same
     * address waits in the same queue. */
    task_block(cur, (void *)(uintptr_t)uaddr);
    return LINUX_FUTEX_BLOCK_WOKEN;
}

static int wrap_wake(const uint32_t *uaddr, int max_waiters) {
    (void)max_waiters;  /* see header note: we wake all */
    task_unblock_channel((void *)(uintptr_t)uaddr);
    /* Linux returns the number woken; we approximate with 1
     * (most pthread mutex/cond wakeups have one waiter). When
     * SpiderMonkey JS atomics land we will need precise counts. */
    return 1;
}

void linux_futex_init_boot(void) {
    static const struct linux_futex_ops ops = {
        .atomic_load_u32 = wrap_atomic_load_u32,
        .block_on        = wrap_block_on,
        .wake            = wrap_wake,
    };
    linux_futex_install_ops(&ops);
}

#endif /* !UNIT_TEST */
