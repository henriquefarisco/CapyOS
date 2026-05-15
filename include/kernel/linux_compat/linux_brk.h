#ifndef KERNEL_LINUX_COMPAT_LINUX_BRK_H
#define KERNEL_LINUX_COMPAT_LINUX_BRK_H

/* Linux ABI `brk(2)` -- the classic heap-extension syscall.
 *
 * Linux semantics (man 2 brk):
 *
 *   brk(0)      -> returns the current program break.
 *   brk(addr)   -> attempts to set the program break to `addr`.
 *                  On success returns the new break (== `addr`).
 *                  On failure returns the *old* break unchanged
 *                  (Linux does NOT return -errno from brk; it
 *                  signals failure by leaving the break put).
 *
 * musl uses `brk` very early in `__libc_start_main` to prime its
 * initial heap (the malloc fallback before mmap is available).
 * Without a real `brk` implementation, musl's `malloc` falls
 * back to `mmap`-only mode, which works but is suboptimal.
 *
 * This module owns a single per-AS heap region:
 *
 *   [LINUX_BRK_BASE, LINUX_BRK_BASE + LINUX_BRK_MAX_SIZE)
 *
 * Live break is `g_brk_current`. Initial state is
 * `LINUX_BRK_BASE` (heap empty). When userland asks for a break
 * higher than current, we round up to a page boundary and call
 * `vmm_register_anon_region` (delegated through the
 * `linux_brk_ops` struct so this module is host-testable).
 * Shrinking the break is a no-op for now (Linux semantics
 * permit either freeing or keeping pages; we keep them).
 *
 * Marco M1 limit: heap caps at LINUX_BRK_MAX_SIZE; further
 * brk() calls return the current break (Linux failure mode).
 * Userland malloc switches to mmap and continues. */

#include <stdint.h>
#include <stddef.h>

/* Heap virtual base. Sits above the mmap arena (which starts at
 * 0x0000_5000_0000_0000 and runs for 1 TiB). Picking a separate
 * range keeps brk and mmap allocations from colliding. */
#define LINUX_BRK_BASE      0x0000600000000000ull

/* Heap maximum size: 256 MiB. Linux defaults are typically much
 * larger but for Marco M1 (musl + SpiderMonkey shell) this is
 * plenty. Larger heaps fall through to mmap. */
#define LINUX_BRK_MAX_SIZE  (256ull * 1024ull * 1024ull)

/* Operations the kernel injects so this module remains
 * host-testable without dragging in the VMM. Tests pass fake
 * impls that just return 0/-1 deterministically. Production
 * boot wiring delegates to `vmm_register_anon_region` for grow
 * and is a no-op for shrink. */
struct linux_brk_ops {
    /* Reserve `pages` virtual pages starting at `start_va`,
     * RW for userland, anon-backed (demand-paged). Returns 0
     * on success, -1 on failure. */
    int (*reserve_pages)(uint64_t start_va, size_t pages);
};

void linux_brk_install_ops(const struct linux_brk_ops *ops);

/* Reset module-local state: live break -> LINUX_BRK_BASE, ops
 * cleared. Tests call this between scenarios; production never
 * invokes it after boot. */
void linux_brk_reset_for_tests(void);

/* Read-only access to the current break. Useful for tests and
 * potentially for `/proc/<pid>/status` if we ever expose VmPeak
 * tracking. */
uint64_t linux_brk_current(void);

/* `brk(new_break)` -- Linux semantics. */
int64_t linux_brk(uint64_t new_break);

/* Register the syscall in the dispatcher. */
void linux_brk_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_BRK_H */
