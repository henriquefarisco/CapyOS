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

/* === Inflight mask accessor (Slice 3F prep) ===================== */

static void test_inflight_mask_empty_after_init(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    if (ahci_slot_inflight_mask(&a) != 0u) {
        fail("initial inflight_mask must be 0");
    }
}

static void test_inflight_mask_null_safe(void) {
    if (ahci_slot_inflight_mask(NULL) != 0u) {
        fail("NULL allocator must yield 0 mask");
    }
}

static void test_inflight_mask_unconfigured_yields_zero(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 0);  /* leaves slot_count = 0 */
    if (ahci_slot_inflight_mask(&a) != 0u) {
        fail("slot_count=0 must yield 0 mask");
    }
}

static void test_inflight_mask_single_alloc(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 4);
    int slot = ahci_slot_alloc(&a);
    if (slot != 0) {
        fail("first alloc must return slot 0");
        return;
    }
    if (ahci_slot_inflight_mask(&a) != 0x1u) {
        fail("single inflight slot 0 must yield mask 0x1");
    }
}

static void test_inflight_mask_multiple_alloc(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 8);
    (void)ahci_slot_alloc(&a);  /* slot 0 */
    (void)ahci_slot_alloc(&a);  /* slot 1 */
    (void)ahci_slot_alloc(&a);  /* slot 2 */
    if (ahci_slot_inflight_mask(&a) != 0x7u) {
        fail("3 sequential allocs must yield mask 0x7");
    }
}

static void test_inflight_mask_after_release(void) {
    struct ahci_slot_allocator a;
    ahci_slot_allocator_init(&a, 8);
    (void)ahci_slot_alloc(&a);   /* slot 0 */
    int s1 = ahci_slot_alloc(&a); /* slot 1 */
    (void)ahci_slot_alloc(&a);   /* slot 2 */
    (void)ahci_slot_release(&a, s1);
    if (ahci_slot_inflight_mask(&a) != 0x5u) {
        fail("release in middle must clear that bit only (mask=0b101)");
    }
}

static void test_inflight_mask_no_bits_above_slot_count(void) {
    /* The mask must NEVER have bits set above slot_count. Even with
     * slot_count=4 fully allocated, bits 4..31 stay zero. The future
     * dispatch fan-in depends on this guarantee. */
    struct ahci_slot_allocator a;
    int i;
    ahci_slot_allocator_init(&a, 4);
    for (i = 0; i < 4; i++) {
        (void)ahci_slot_alloc(&a);
    }
    uint32_t mask = ahci_slot_inflight_mask(&a);
    if (mask != 0x0Fu) {
        fail("fully-allocated 4-slot inflight must yield exactly 0x0F");
    }
    if ((mask >> 4) != 0u) {
        fail("bits above slot_count must be zero");
    }
}

static void test_inflight_mask_full_32(void) {
    struct ahci_slot_allocator a;
    int i;
    ahci_slot_allocator_init(&a, AHCI_MAX_SLOTS);
    for (i = 0; i < AHCI_MAX_SLOTS; i++) {
        (void)ahci_slot_alloc(&a);
    }
    if (ahci_slot_inflight_mask(&a) != 0xFFFFFFFFu) {
        fail("32 fully-allocated must yield mask 0xFFFFFFFF");
    }
}

static void test_inflight_mask_matches_count(void) {
    /* popcount(inflight_mask) must equal inflight_count for any
     * sequence of alloc/release operations. */
    struct ahci_slot_allocator a;
    int s0, s2, s5;
    ahci_slot_allocator_init(&a, 8);
    s0 = ahci_slot_alloc(&a);
    (void)ahci_slot_alloc(&a); /* s1 */
    s2 = ahci_slot_alloc(&a);
    (void)ahci_slot_alloc(&a); /* s3 */
    (void)ahci_slot_alloc(&a); /* s4 */
    s5 = ahci_slot_alloc(&a);
    (void)ahci_slot_release(&a, s0);
    (void)ahci_slot_release(&a, s2);
    (void)ahci_slot_release(&a, s5);
    /* Remaining inflight: s1, s3, s4 -> mask 0b011010 = 0x1A */
    uint32_t mask = ahci_slot_inflight_mask(&a);
    uint8_t count = ahci_slot_inflight_count(&a);
    if (mask != 0x1Au) {
        fail("inflight_mask must be 0x1A after specific alloc/release pattern");
    }
    if (count != 3) {
        fail("inflight_count must be 3 after specific alloc/release pattern");
    }
    /* Cross-check: popcount(mask) == count. */
    uint8_t popcount = 0;
    uint32_t tmp = mask;
    while (tmp) {
        tmp &= tmp - 1u;
        popcount++;
    }
    if (popcount != count) {
        fail("popcount(inflight_mask) must equal inflight_count");
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
    test_inflight_mask_empty_after_init();
    test_inflight_mask_null_safe();
    test_inflight_mask_unconfigured_yields_zero();
    test_inflight_mask_single_alloc();
    test_inflight_mask_multiple_alloc();
    test_inflight_mask_after_release();
    test_inflight_mask_no_bits_above_slot_count();
    test_inflight_mask_full_32();
    test_inflight_mask_matches_count();
    if (g_failures == 0) printf("[tests] ahci_slot_allocator OK\n");
    return g_failures;
}
