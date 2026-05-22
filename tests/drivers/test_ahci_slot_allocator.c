/* Host tests for the AHCI slot allocator (Slice 3E.3). */

#include "drivers/storage/ahci_slot_allocator.h"

#include <stdint.h>
#include <stdio.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[ahci-slot] FAIL: %s\n", msg);
    g_failures++;
}

static void test_init_typical(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    if (a.slot_count != 4) fail("slot_count must persist");
    if (a.free_mask != 0x0Fu) fail("free_mask must mark exactly 4 slots free");
    if (ahci_slot_inflight_count(&a) != 0) {
        fail("initial inflight must be 0");
    }
}

static void test_init_full_32(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, AHCI_MAX_SLOTS);
    if (a.free_mask != 0xFFFFFFFFu) {
        fail("32-slot init must set free_mask to all-ones");
    }
}

static void test_init_invalid_disables_allocator(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 0);
    if (ahci_slot_alloc(&a) != -1) {
        fail("slot_count=0 must produce an unusable allocator");
    }
    ahci_slot_allocator_init(&a, 33);
    if (ahci_slot_alloc(&a) != -1) {
        fail("slot_count>32 must produce an unusable allocator");
    }
}

static void test_alloc_lowest_first(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    if (ahci_slot_alloc(&a) != 0) fail("first alloc must be slot 0");
    if (ahci_slot_alloc(&a) != 1) fail("second alloc must be slot 1");
    if (ahci_slot_alloc(&a) != 2) fail("third alloc must be slot 2");
    if (ahci_slot_inflight_count(&a) != 3) {
        fail("inflight must be 3 after 3 allocs");
    }
}

static void test_alloc_until_full_then_fail(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    for (int i = 0; i < 4; i++) {
        if (ahci_slot_alloc(&a) != i) {
            fail("alloc must hand out sequential slots");
        }
    }
    if (ahci_slot_alloc(&a) != -1) {
        fail("alloc must return -1 when all slots are inflight");
    }
    if (ahci_slot_inflight_count(&a) != 4) {
        fail("inflight must equal slot_count when full");
    }
}

static void test_release_reuses_slot(void) {
    struct ahci_slot_allocator a;
    int s0, s1, s_again;
    ahci_slot_allocator_init(&a, 4);
    s0 = ahci_slot_alloc(&a);
    s1 = ahci_slot_alloc(&a);
    if (ahci_slot_release(&a, s0) != 0) fail("release of allocated slot must succeed");
    /* After releasing slot 0, the next alloc must return slot 0
     * again (lowest-numbered free). */
    s_again = ahci_slot_alloc(&a);
    if (s_again != s0) fail("released slot must be reused");
    if (ahci_slot_inflight_count(&a) != 2) {
        fail("inflight must be 2 (s1 and s_again)");
    }
    (void)s1;
}

static void test_release_invalid_inputs(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    if (ahci_slot_release(&a, -1) != -1) {
        fail("negative slot must be rejected");
    }
    if (ahci_slot_release(&a, 4) != -1) {
        fail("out-of-range slot must be rejected");
    }
    if (ahci_slot_release(&a, 0) != -1) {
        fail("releasing a slot that was never allocated must fail");
    }
    if (ahci_slot_release(NULL, 0) != -1) fail("NULL alloc must be rejected");
}

static void test_double_release_is_rejected(void) {
    struct ahci_slot_allocator a;
    int s;
    ahci_slot_allocator_init(&a, 4);
    s = ahci_slot_alloc(&a);
    if (ahci_slot_release(&a, s) != 0) fail("first release must succeed");
    if (ahci_slot_release(&a, s) != -1) {
        fail("double release must be rejected");
    }
}

static void test_is_free_accessor(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    if (!ahci_slot_is_free(&a, 0)) fail("slot 0 must be free after init");
    if (ahci_slot_is_free(&a, 4)) fail("slot beyond range must NOT report free");
    (void)ahci_slot_alloc(&a);
    if (ahci_slot_is_free(&a, 0)) fail("slot 0 must NOT be free after alloc");
}

static void test_reset_marks_all_free(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    (void)ahci_slot_alloc(&a);
    (void)ahci_slot_alloc(&a);
    if (ahci_slot_inflight_count(&a) != 2) fail("setup: inflight=2");
    ahci_slot_allocator_reset(&a);
    if (ahci_slot_inflight_count(&a) != 0) {
        fail("reset must mark all slots free");
    }
    if (ahci_slot_alloc(&a) != 0) {
        fail("after reset, first alloc must be slot 0 again");
    }
}

static void test_inflight_count_full_32(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, AHCI_MAX_SLOTS);
    for (int i = 0; i < AHCI_MAX_SLOTS; i++) {
        if (ahci_slot_alloc(&a) != i) {
            fail("32-slot alloc must hand out sequential indices");
            return;
        }
    }
    if (ahci_slot_inflight_count(&a) != AHCI_MAX_SLOTS) {
        fail("inflight must be 32 when fully allocated");
    }
    if (ahci_slot_alloc(&a) != -1) {
        fail("33rd alloc must fail on 32-slot allocator");
    }
}

int run_ahci_slot_allocator_tests(void) {
    g_failures = 0;
    test_init_typical();
    test_init_full_32();
    test_init_invalid_disables_allocator();
    test_alloc_lowest_first();
    test_alloc_until_full_then_fail();
    test_release_reuses_slot();
    test_release_invalid_inputs();
    test_double_release_is_rejected();
    test_is_free_accessor();
    test_reset_marks_all_free();
    test_inflight_count_full_32();
    if (g_failures == 0) printf("[tests] ahci_slot_allocator OK\n");
    return g_failures;
}
