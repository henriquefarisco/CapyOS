#ifndef KERNEL_LINUX_COMPAT_LINUX_MLOCK_H
#define KERNEL_LINUX_COMPAT_LINUX_MLOCK_H

/* Linux ABI memory locking syscalls.
 *
 *   int mlock  (const void *addr, size_t len);
 *   int munlock(const void *addr, size_t len);
 *   int mlockall  (int flags);
 *   int munlockall(void);
 *
 * Why this matters for the Firefox port:
 *   - SpiderMonkey's JIT calls `mlock` on its W^X executable pages
 *     to prevent swap-out (the JIT keeps generated code resident
 *     to avoid icache stalls on page-back-in). On -ENOSYS the JIT
 *     falls back to `madvise(DONTNEED)` heuristics that thrash.
 *   - musl `pthread_setspecific` and TLS bring-up call `mlock` on
 *     the TLS area to guarantee deterministic latency for thread
 *     locals; without it the path returns -EAGAIN and pthread
 *     creation aborts.
 *
 * Marco M1 has no swap and never pages anything out, so the
 * locks are trivially satisfied. We accept the calls with full
 * Linux validation:
 *   - len == 0 returns 0 (Linux: short-circuit).
 *   - addr+len overflow -> -EINVAL.
 *   - mlockall flags outside MCL_KNOWN_FLAGS -> -EINVAL.
 *   - everything else returns 0 (no-op success).
 *
 * When a real swapper / page reclaim lands the validation here
 * stays the same; only the success path swaps to actually
 * pinning physical frames. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_MCL_CURRENT  0x01
#define LINUX_MCL_FUTURE   0x02
#define LINUX_MCL_ONFAULT  0x04
#define LINUX_MCL_KNOWN_FLAGS \
    (LINUX_MCL_CURRENT | LINUX_MCL_FUTURE | LINUX_MCL_ONFAULT)

int64_t linux_mlock     (uint64_t addr, size_t len);
int64_t linux_munlock   (uint64_t addr, size_t len);
int64_t linux_mlockall  (int flags);
int64_t linux_munlockall(void);

void linux_mlock_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_MLOCK_H */
