/*
 * Copy-on-write decision module (M4 phase 7c).
 *
 * Pure function: no inline asm, no global state, no I/O. The body
 * is intentionally tiny so the host unit-test binary can lock the
 * full decision matrix in tests/test_vmm_cow.c.
 *
 * Why split this out?
 *   The actual fault servicing path (allocate frame, copy 4KiB,
 *   walk PML4, write PTE, invlpg) is x86_64-specific and lives in
 *   src/memory/vmm.c which cannot be linked into the host binary
 *   (cr3, invlpg, ...). By isolating the *policy* in a pure
 *   function we get a regression test without simulating the
 *   walker.
 */
#include "memory/vmm_cow.h"
#include "memory/vmm.h"

struct vmm_cow_decision vmm_cow_decide(uint64_t pte,
                                       uint16_t refcount_after_dec) {
    struct vmm_cow_decision d = {0};

    /* If the PTE was not flagged CoW by the cloner, this fault is
     * a genuine RW-on-RO violation and must NOT be recovered. The
     * dispatcher will escalate to KILL_PROCESS. */
    if (!(pte & VMM_PAGE_COW)) {
        d.action = VMM_COW_NOT_COW;
        return d;
    }

    /* From here on we know the PTE was a clone-time RO share. The
     * decision is purely between "reuse in place" (last sharer) and
     * "allocate copy" (still shared). The new PTE always wants
     * WRITE set and COW cleared regardless. */
    d.new_set = VMM_PAGE_WRITE;
    d.new_clr = VMM_PAGE_COW;

    if (refcount_after_dec == 0u) {
        /* We were the last sharer. The frame is now privately owned
         * by us so just flip RW back on. */
        d.action = VMM_COW_REUSE;
    } else {
        /* Other AS still hold this frame. We must get our own copy
         * before writing, otherwise the write would leak into the
         * sibling's address space. */
        d.action = VMM_COW_COPY;
    }
    return d;
}
