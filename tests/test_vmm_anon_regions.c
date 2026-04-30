/*
 * Host tests for the demand-paging anonymous-region registry
 * (M4 phase 7b).
 *
 * The registry itself is host-friendly: it lives in
 * src/memory/vmm_regions.c and is just kmalloc-backed linked-list
 * manipulation, so the test pulls in the real production code rather
 * than a stub. The actual page-fault servicing in
 * src/memory/vmm.c::vmm_handle_page_fault uses x86_64 inline asm and
 * is not exercised here; that path is validated by the QEMU smoke
 * tests once a userland program triggers a real demand fault.
 *
 * Coverage:
 *   - register/find/clear lifecycle on a fresh address space
 *   - rejection of NULL inputs, zero-length ranges, overflow ranges
 *   - rejection of overlapping registrations (any kind of overlap)
 *   - find returns the right region across multiple non-overlapping
 *     registrations and reports NULL for misses
 *   - find treats each interval as half-open [start, end)
 *   - clear is idempotent and safely handles an empty list
 *   - rss accessor returns the per-AS counter (not the global one)
 *   - vmm_destroy_address_space (via the host stub) frees the region
 *     list without leaking
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "memory/vmm.h"

static int tests_run = 0;
static int tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS()     do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg)  printf("FAIL: %s\n", msg)

/* The host stub_vmm.c gives us calloc-backed AS handles; using the
 * real vmm_create_address_space is good enough because it zero-inits
 * `anon_regions` and `rss_pages` (the only fields the registry
 * touches). */
static struct vmm_address_space *fresh_as(void) {
    struct vmm_address_space *as = vmm_create_address_space();
    return as;
}

static void test_empty_as(void) {
    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    TEST("fresh AS: anon_regions == NULL");
    if (as->anon_regions == NULL) PASS();
    else FAIL("not zero-init");

    TEST("fresh AS: rss_pages == 0");
    if (vmm_address_space_rss(as) == 0) PASS();
    else FAIL("rss not zero");

    TEST("fresh AS: find returns NULL for any address");
    if (vmm_anon_region_find(as, 0x1000) == NULL) PASS();
    else FAIL("phantom region");

    vmm_destroy_address_space(as);
}

static void test_register_basic(void) {
    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    int rc = vmm_register_anon_region(as, 0x10000, 4,
                                      VMM_PAGE_USER | VMM_PAGE_WRITE);
    TEST("register 4 pages at 0x10000 returns 0");
    if (rc == 0) PASS();
    else FAIL("non-zero rc");

    TEST("register: anon_regions head is non-NULL");
    if (as->anon_regions != NULL) PASS();
    else FAIL("head still NULL");

    TEST("register: head start == 0x10000");
    if (as->anon_regions->start == 0x10000) PASS();
    else FAIL("start mismatch");

    TEST("register: head end == 0x10000 + 4*PAGE_SIZE");
    if (as->anon_regions->end == 0x10000 + 4ull * VMM_PAGE_SIZE) PASS();
    else FAIL("end mismatch");

    TEST("register: head flags preserve user+write");
    if ((as->anon_regions->flags & VMM_PAGE_USER) &&
        (as->anon_regions->flags & VMM_PAGE_WRITE)) PASS();
    else FAIL("flags lost");

    vmm_destroy_address_space(as);
}

static void test_register_rejects_bad_inputs(void) {
    int rc;

    rc = vmm_register_anon_region(NULL, 0x1000, 1, 0);
    TEST("register NULL as -> -1");
    if (rc == -1) PASS();
    else FAIL("expected -1");

    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    rc = vmm_register_anon_region(as, 0x1000, 0, 0);
    TEST("register page_count=0 -> -1");
    if (rc == -1) PASS();
    else FAIL("expected -1");

    /* Overflow: start near top of 64-bit, page_count makes end wrap
     * around. Helper must reject without writing anything. */
    rc = vmm_register_anon_region(as, ~(uint64_t)0 - 100,
                                  10, 0);
    TEST("register with start+size overflow -> -1");
    if (rc == -1) PASS();
    else FAIL("overflow not caught");

    vmm_destroy_address_space(as);
}

static void test_register_rejects_overlap(void) {
    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    /* Establish an anchor region [0x10000, 0x14000) (4 pages). */
    int rc = vmm_register_anon_region(as, 0x10000, 4, 0);
    if (rc != 0) { TEST("setup anchor"); FAIL("register"); goto out; }

    TEST("overlap: identical range rejected");
    if (vmm_register_anon_region(as, 0x10000, 4, 0) == -1) PASS();
    else FAIL("identical accepted");

    TEST("overlap: subset range rejected");
    if (vmm_register_anon_region(as, 0x11000, 2, 0) == -1) PASS();
    else FAIL("subset accepted");

    TEST("overlap: superset range rejected");
    if (vmm_register_anon_region(as, 0x0F000, 8, 0) == -1) PASS();
    else FAIL("superset accepted");

    TEST("overlap: left-edge nibble rejected");
    if (vmm_register_anon_region(as, 0x0F000, 2, 0) == -1) PASS();
    else FAIL("left edge accepted");

    TEST("overlap: right-edge nibble rejected");
    if (vmm_register_anon_region(as, 0x13000, 2, 0) == -1) PASS();
    else FAIL("right edge accepted");

    /* Adjacent (non-overlapping) ranges are fine: end is exclusive. */
    TEST("non-overlap: range starting exactly at end accepted");
    if (vmm_register_anon_region(as, 0x14000, 2, 0) == 0) PASS();
    else FAIL("adjacent rejected");

    TEST("non-overlap: range ending exactly at start accepted");
    if (vmm_register_anon_region(as, 0x0E000, 2, 0) == 0) PASS();
    else FAIL("adjacent (left) rejected");

out:
    vmm_destroy_address_space(as);
}

static void test_find_across_multiple_regions(void) {
    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    /* Three non-overlapping regions:
     *   A = [0x10000, 0x12000) - 2 pages
     *   B = [0x20000, 0x21000) - 1 page
     *   C = [0x30000, 0x34000) - 4 pages
     */
    if (vmm_register_anon_region(as, 0x10000, 2, 0) != 0 ||
        vmm_register_anon_region(as, 0x20000, 1, 0) != 0 ||
        vmm_register_anon_region(as, 0x30000, 4, 0) != 0) {
        TEST("setup 3 regions"); FAIL("register"); goto out;
    }

    TEST("find: hit inside A reports A");
    struct vmm_anon_region *r = vmm_anon_region_find(as, 0x10800);
    if (r && r->start == 0x10000) PASS();
    else FAIL("wrong region");

    TEST("find: hit inside B reports B");
    r = vmm_anon_region_find(as, 0x20FFF);
    if (r && r->start == 0x20000) PASS();
    else FAIL("wrong region");

    TEST("find: hit at the inclusive start of C");
    r = vmm_anon_region_find(as, 0x30000);
    if (r && r->start == 0x30000) PASS();
    else FAIL("inclusive start missed");

    TEST("find: address at exclusive end of C is a miss");
    r = vmm_anon_region_find(as, 0x34000);
    if (r == NULL) PASS();
    else FAIL("end is not exclusive");

    TEST("find: gap between A and B is a miss");
    r = vmm_anon_region_find(as, 0x15000);
    if (r == NULL) PASS();
    else FAIL("phantom in gap");

    TEST("find: well-below-everything is a miss");
    r = vmm_anon_region_find(as, 0x0);
    if (r == NULL) PASS();
    else FAIL("phantom at zero");

out:
    vmm_destroy_address_space(as);
}

static void test_clear_idempotent(void) {
    struct vmm_address_space *as = fresh_as();
    if (!as) { TEST("setup"); FAIL("create"); return; }

    /* Two regions then clear -> empty. */
    if (vmm_register_anon_region(as, 0x10000, 2, 0) != 0 ||
        vmm_register_anon_region(as, 0x20000, 2, 0) != 0) {
        TEST("setup"); FAIL("register"); goto out;
    }

    vmm_clear_anon_regions(as);

    TEST("clear: anon_regions == NULL");
    if (as->anon_regions == NULL) PASS();
    else FAIL("head not reset");

    TEST("clear: subsequent find misses");
    if (vmm_anon_region_find(as, 0x10000) == NULL) PASS();
    else FAIL("region still findable");

    /* Idempotent: a second clear must not crash and must keep the
     * AS in a consistent state. */
    vmm_clear_anon_regions(as);

    TEST("clear: second call is a no-op");
    if (as->anon_regions == NULL) PASS();
    else FAIL("second clear corrupted state");

    /* After clear, registration still works. */
    int rc = vmm_register_anon_region(as, 0x10000, 1, 0);
    TEST("clear: re-register after clear succeeds");
    if (rc == 0) PASS();
    else FAIL("re-register failed");

out:
    vmm_destroy_address_space(as);
}

static void test_clear_null_safe(void) {
    /* Behaviour contract: NULL is a no-op (not a crash). */
    vmm_clear_anon_regions(NULL);
    TEST("clear(NULL) is a no-op");
    PASS();

    TEST("rss(NULL) is 0");
    if (vmm_address_space_rss(NULL) == 0) PASS();
    else FAIL("rss(NULL) returned non-zero");

    TEST("find(NULL, ...) returns NULL");
    if (vmm_anon_region_find(NULL, 0x1234) == NULL) PASS();
    else FAIL("find(NULL) returned non-NULL");
}

static void test_rss_per_as(void) {
    /* The RSS counter is per-AS, NOT global. Two address spaces
     * must keep independent counters. The registry does NOT
     * itself bump rss (that is map_page's job); we manipulate the
     * field directly to keep the test self-contained. */
    struct vmm_address_space *a = fresh_as();
    struct vmm_address_space *b = fresh_as();
    if (!a || !b) { TEST("setup"); FAIL("create"); return; }

    a->rss_pages = 7;
    b->rss_pages = 3;

    TEST("rss: AS a reports its own counter");
    if (vmm_address_space_rss(a) == 7) PASS();
    else FAIL("a counter wrong");

    TEST("rss: AS b reports its own counter");
    if (vmm_address_space_rss(b) == 3) PASS();
    else FAIL("b counter wrong");

    /* Reset before destroy so the host destroy_stub does not
     * misinterpret leftover state. */
    a->rss_pages = 0;
    b->rss_pages = 0;
    vmm_destroy_address_space(a);
    vmm_destroy_address_space(b);
}

int test_vmm_anon_regions_run(void) {
    printf("[test_vmm_anon_regions]\n");
    tests_run = 0;
    tests_passed = 0;

    test_empty_as();
    test_register_basic();
    test_register_rejects_bad_inputs();
    test_register_rejects_overlap();
    test_find_across_multiple_regions();
    test_clear_idempotent();
    test_clear_null_safe();
    test_rss_per_as();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
