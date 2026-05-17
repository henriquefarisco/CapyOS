/*
 * Host tests for the M4 phase 8f.1 TSS scaffolding.
 *
 * Locks two contracts:
 *
 *   1. The hardware-mandated layout of `struct tss` (offsets and
 *      total size) so a future field addition cannot silently
 *      drift the asm-visible RSP0 slot.
 *   2. The encoding of the 64-bit TSS GDT descriptor produced by
 *      `tss_descriptor_low / tss_descriptor_high`. The raw bit
 *      layout is dictated by Intel SDM Vol 3 Section 7.2.3 and
 *      this is the only place we re-spell it without referring to
 *      the implementation, so a regression in the encoder is
 *      caught here.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "arch/x86_64/tss.h"

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

static void test_tss_struct_layout(void) {
    TEST("sizeof(struct tss) == 104 (TSS_SIZE)");
    if (sizeof(struct tss) == 104u && TSS_SIZE == 104u) PASS();
    else FAIL("TSS struct/size constant drift");

    TEST("offsetof(reserved1) == 0x00");
    if (offsetof(struct tss, reserved1) == 0u) PASS();
    else FAIL("reserved1 offset drift");

    TEST("offsetof(rsp0) == 0x04 (TSS_RSP0_OFFSET)");
    if (offsetof(struct tss, rsp0) == 0x04u &&
        TSS_RSP0_OFFSET == 0x04u) PASS();
    else FAIL("rsp0 offset drift");

    TEST("offsetof(rsp1) == 0x0C");
    if (offsetof(struct tss, rsp1) == 0x0Cu) PASS();
    else FAIL("rsp1 offset drift");

    TEST("offsetof(rsp2) == 0x14");
    if (offsetof(struct tss, rsp2) == 0x14u) PASS();
    else FAIL("rsp2 offset drift");

    TEST("offsetof(ist1) == 0x24");
    if (offsetof(struct tss, ist1) == 0x24u) PASS();
    else FAIL("ist1 offset drift");

    TEST("offsetof(iomap_base) == 0x66 (TSS_IOMAP_BASE_OFFSET)");
    if (offsetof(struct tss, iomap_base) == 0x66u &&
        TSS_IOMAP_BASE_OFFSET == 0x66u) PASS();
    else FAIL("iomap_base offset drift");
}

/* Helpers for asserting bitfield ranges. */
static uint64_t bits(uint64_t x, int hi, int lo) {
    uint64_t mask;
    int width = hi - lo + 1;
    if (width == 64) mask = ~0ULL;
    else mask = ((1ULL << width) - 1ULL);
    return (x >> lo) & mask;
}

static void test_descriptor_low_layout(void) {
    /* Use a base/limit that exercises every byte of the encoding. */
    uint64_t base = 0xAABBCCDD11223344ULL;
    uint32_t limit = 0x000FAA67u; /* 20-bit max */
    uint64_t low = tss_descriptor_low(base, limit, 0u);

    TEST("desc_low: limit_low [15:0] = limit & 0xFFFF");
    if (bits(low, 15, 0) == (limit & 0xFFFFu)) PASS();
    else FAIL("limit_low encoding wrong");

    TEST("desc_low: base_low [31:16] = base[15:0]");
    if (bits(low, 31, 16) == (base & 0xFFFFu)) PASS();
    else FAIL("base_low encoding wrong");

    TEST("desc_low: base_mid [39:32] = base[23:16]");
    if (bits(low, 39, 32) == ((base >> 16) & 0xFFu)) PASS();
    else FAIL("base_mid encoding wrong");

    TEST("desc_low: access [47:40] = 0x89 for DPL=0 64-bit TSS");
    /* 0x80 (P=1) | 0 (DPL=0 << 5) | 0x09 (type=available 64-bit
     * TSS) = 0x89. */
    if (bits(low, 47, 40) == 0x89u) PASS();
    else FAIL("access byte not 0x89");

    TEST("desc_low: limit_high [51:48] = limit[19:16]");
    if (bits(low, 51, 48) == ((limit >> 16) & 0x0Fu)) PASS();
    else FAIL("limit_high encoding wrong");

    TEST("desc_low: base_high [63:56] = base[31:24]");
    if (bits(low, 63, 56) == ((base >> 24) & 0xFFu)) PASS();
    else FAIL("base_high encoding wrong");
}

static void test_descriptor_dpl_dpl3(void) {
    /* Kernel TSS is always DPL=0 in CapyOS, but the encoder must
     * accept arbitrary DPLs because nothing prevents a future
     * caller from passing DPL=3 by mistake. We only lock the bit
     * layout, not the policy. */
    uint64_t low_dpl3 = tss_descriptor_low(0, 0, 3u);
    /* DPL=3 access byte = 0x80 | (3<<5)=0x60 | 0x09 = 0xE9. */
    TEST("desc_low DPL=3 sets bits 5..6 of access byte");
    if (bits(low_dpl3, 47, 40) == 0xE9u) PASS();
    else FAIL("DPL bits not encoded correctly");
}

static void test_descriptor_high_layout(void) {
    uint64_t base = 0xAABBCCDD11223344ULL;
    uint64_t high = tss_descriptor_high(base);

    TEST("desc_high: low 32 bits = base[63:32]");
    if (bits(high, 31, 0) == ((base >> 32) & 0xFFFFFFFFull)) PASS();
    else FAIL("base_upper encoding wrong");

    TEST("desc_high: high 32 bits reserved (must be 0)");
    if (bits(high, 63, 32) == 0u) PASS();
    else FAIL("high reserved bits non-zero");
}

static void test_set_get_rsp0_round_trip(void) {
    /* These are the only host-callable accessors that touch state.
     * tss_init does LTR which is x86_64-only and gated; the
     * accessors here are pure C. */
    tss_set_rsp0(0xCAFE000000000010ULL);
    TEST("tss_set_rsp0 + tss_get_rsp0 round-trip");
    if (tss_get_rsp0() == 0xCAFE000000000010ULL) PASS();
    else FAIL("rsp0 round-trip mismatch");
    /* Restore to 0 so subsequent test runs see a clean slate. */
    tss_set_rsp0(0);
}

int test_tss_layout_run(void) {
    printf("[test_tss_layout]\n");
    tests_run = 0;
    tests_passed = 0;
    test_tss_struct_layout();
    test_descriptor_low_layout();
    test_descriptor_dpl_dpl3();
    test_descriptor_high_layout();
    test_set_get_rsp0_round_trip();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
