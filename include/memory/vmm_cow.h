#ifndef MEMORY_VMM_COW_H
#define MEMORY_VMM_COW_H

#include <stdint.h>

/*
 * Copy-on-write decision module (M4 phase 7c).
 *
 * `vmm_handle_page_fault` invokes this to decide what to do with a
 * present-but-RO PTE when the user wrote to it. The body is split
 * out as a pure function (no inline asm, no global state, no
 * physical-page side effects) so the host unit-test binary can lock
 * the decision matrix without simulating the page-table walker.
 *
 * Inputs:
 *   - pte       : the current PTE value (P, RW, US, ..., COW, ...).
 *   - refcount_after_dec : the result of `pmm_frame_refcount_dec`
 *                 against the underlying physical frame BEFORE the
 *                 fault handler installs any new mapping. The caller
 *                 is responsible for performing the decrement.
 *
 * Output:
 *   - action    : tells the caller what to do next.
 *   - new_set   : flags to OR into the PTE before re-installing it
 *                 (typically VMM_PAGE_WRITE).
 *   - new_clr   : flags to CLEAR from the PTE before re-installing it
 *                 (typically VMM_PAGE_COW).
 *
 * The function is total - every legal input maps to exactly one of
 * the three actions. The kernel-side glue then (a) optionally
 * allocates+copies a fresh frame and (b) writes the resulting PTE
 * back into the table.
 */

enum vmm_cow_action {
    /* The PTE is not actually a CoW share. Caller should refuse to
     * recover (return -1 from vmm_handle_page_fault) and let the
     * dispatcher escalate to KILL_PROCESS. */
    VMM_COW_NOT_COW = 0,
    /* The faulting AS is the LAST sharer of the underlying frame
     * (refcount_after_dec == 0). Caller should clear COW + set
     * VMM_PAGE_WRITE on the existing PTE in place; no copy needed. */
    VMM_COW_REUSE = 1,
    /* The frame is still shared by other AS (refcount_after_dec > 0).
     * Caller must allocate a new frame, copy the page contents from
     * the old physical frame, and install a fresh PTE pointing at the
     * copy with VMM_PAGE_WRITE set and VMM_PAGE_COW cleared. */
    VMM_COW_COPY = 2,
};

struct vmm_cow_decision {
    enum vmm_cow_action action;
    uint64_t new_set; /* bits to OR into the next PTE  */
    uint64_t new_clr; /* bits to AND-NOT into the next PTE */
};

struct vmm_cow_decision vmm_cow_decide(uint64_t pte,
                                       uint16_t refcount_after_dec);

#endif /* MEMORY_VMM_COW_H */
