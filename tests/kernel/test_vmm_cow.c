/*
 * Host tests for the copy-on-write decision module (M4 phase 7c).
 *
 * vmm_cow_decide is a pure function so the entire decision matrix
 * can be locked here without simulating the page-table walker. The
 * matrix has only 4 dimensions:
 *
 *   - VMM_PAGE_COW present in the PTE (yes/no)
 *   - refcount_after_dec (0 / >0)
 *
 * Every legal cell maps to exactly one of three actions:
 *   NOT_COW : not actually a CoW share, dispatcher must KILL_PROCESS.
 *   REUSE   : last sharer, flip RW back in place.
 *   COPY    : still shared, allocate fresh frame and copy.
 *
 * The "new_set / new_clr" fields are used by the kernel-side glue to
 * compute the next PTE value: pte = (pte | new_set) & ~new_clr.
 */
#include <stdio.h>
#include <stdint.h>

#include "memory/vmm.h"
#include "memory/vmm_cow.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do {                                                                   \
        printf("OK\n");                                                    \
        tests_passed++;                                                    \
    } while (0)
#define FAIL(msg)                                                          \
    do { printf("FAIL: %s\n", msg); } while (0)

/* A representative "RO + CoW share" PTE: present, user, RO. */
#define COW_SHARED_PTE                                                     \
    (VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_COW | 0x4242000ULL)
/* A representative "explicit RO mapping" PTE: present, user, RO,    */
/* but NOT marked CoW (think text segment / mprotect-RO).            */
#define EXPLICIT_RO_PTE                                                    \
    (VMM_PAGE_PRESENT | VMM_PAGE_USER | 0x4242000ULL)

static void test_pte_without_cow_bit_is_not_cow(void) {
    struct vmm_cow_decision d = vmm_cow_decide(EXPLICIT_RO_PTE, 0u);
    TEST("PTE without COW bit -> NOT_COW (refcount=0)");
    if (d.action == VMM_COW_NOT_COW) PASS();
    else FAIL("explicit RO PTE leaked into CoW arm");
    d = vmm_cow_decide(EXPLICIT_RO_PTE, 5u);
    TEST("PTE without COW bit -> NOT_COW (refcount=5)");
    if (d.action == VMM_COW_NOT_COW) PASS();
    else FAIL("explicit RO PTE leaked into CoW arm");
}

static void test_pte_with_cow_bit_last_sharer_reuses(void) {
    struct vmm_cow_decision d = vmm_cow_decide(COW_SHARED_PTE, 0u);
    TEST("COW share + refcount==0 -> REUSE");
    if (d.action == VMM_COW_REUSE) PASS();
    else FAIL("last sharer did not pick REUSE");
    TEST("REUSE sets WRITE flag");
    if ((d.new_set & VMM_PAGE_WRITE) == VMM_PAGE_WRITE) PASS();
    else FAIL("REUSE forgot to add WRITE");
    TEST("REUSE clears COW flag");
    if ((d.new_clr & VMM_PAGE_COW) == VMM_PAGE_COW) PASS();
    else FAIL("REUSE forgot to clear COW");
}

static void test_pte_with_cow_bit_still_shared_copies(void) {
    struct vmm_cow_decision d = vmm_cow_decide(COW_SHARED_PTE, 1u);
    TEST("COW share + refcount==1 -> COPY");
    if (d.action == VMM_COW_COPY) PASS();
    else FAIL("still-shared CoW did not pick COPY");
    d = vmm_cow_decide(COW_SHARED_PTE, 7u);
    TEST("COW share + refcount==7 -> COPY");
    if (d.action == VMM_COW_COPY) PASS();
    else FAIL("still-shared CoW did not pick COPY");
}

static void test_copy_decision_emits_correct_flag_deltas(void) {
    struct vmm_cow_decision d = vmm_cow_decide(COW_SHARED_PTE, 4u);

    /* Compute the would-be new PTE the kernel-side glue will assemble.
     * Note: in the COPY arm, the kernel actually replaces the
     * physical address too, so this composition only models the flag
     * mutation. */
    uint64_t next = (COW_SHARED_PTE | d.new_set) & ~d.new_clr;

    TEST("COPY result has WRITE set");
    if ((next & VMM_PAGE_WRITE) == VMM_PAGE_WRITE) PASS();
    else FAIL("WRITE missing on COPY");
    TEST("COPY result has COW cleared");
    if ((next & VMM_PAGE_COW) == 0u) PASS();
    else FAIL("COW leaked into post-copy PTE");
    TEST("COPY result keeps PRESENT");
    if ((next & VMM_PAGE_PRESENT) == VMM_PAGE_PRESENT) PASS();
    else FAIL("PRESENT lost across CoW resolution");
    TEST("COPY result keeps USER");
    if ((next & VMM_PAGE_USER) == VMM_PAGE_USER) PASS();
    else FAIL("USER lost across CoW resolution");
}

static void test_not_cow_emits_neutral_flag_deltas(void) {
    /* The NOT_COW arm should not bother setting/clearing flags; the
     * caller will reject the recovery anyway. We document and lock
     * that contract here. */
    struct vmm_cow_decision d = vmm_cow_decide(EXPLICIT_RO_PTE, 9u);
    TEST("NOT_COW arm does not set WRITE (caller bails)");
    if ((d.new_set & VMM_PAGE_WRITE) == 0u) PASS();
    else FAIL("NOT_COW would have spuriously enabled WRITE");
    TEST("NOT_COW arm does not clear COW (caller bails)");
    if ((d.new_clr & VMM_PAGE_COW) == 0u) PASS();
    else FAIL("NOT_COW arm spuriously cleared COW on a non-CoW PTE");
}

int test_vmm_cow_run(void) {
    printf("[test_vmm_cow]\n");
    tests_run = 0;
    tests_passed = 0;
    test_pte_without_cow_bit_is_not_cow();
    test_pte_with_cow_bit_last_sharer_reuses();
    test_pte_with_cow_bit_still_shared_copies();
    test_copy_decision_emits_correct_flag_deltas();
    test_not_cow_emits_neutral_flag_deltas();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
