/*
 * x86_64 Task State Segment scaffolding (M4 phase 8f.1).
 *
 * Public surface in include/arch/x86_64/tss.h. The pure encoder
 * helpers (tss_descriptor_low / tss_descriptor_high) and the
 * runtime accessors (tss_get_rsp0 / tss_set_rsp0) are host-testable;
 * tss_init() is the only function that touches the CPU (LTR) and is
 * therefore guarded by `__x86_64__ && !UNIT_TEST` for the LTR
 * instruction.
 */
#include "arch/x86_64/tss.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h" /* shares g_gdt via interrupts.c */

/* The boot TSS lives in this TU. Single-CPU for now; an SMP step
 * will replace this with a per-CPU array indexed by APIC ID. */
static struct tss g_boot_tss;
static int g_tss_loaded = 0;

/* GDT slot index where the 64-bit TSS descriptor lives. The 64-bit
 * TSS descriptor is 16 bytes (occupies two 8-byte GDT slots) so
 * `interrupts.c::gdt_init` reserves slots 5 + 6. The corresponding
 * selector is 5 * 8 = 0x28. */
#define TSS_GDT_SLOT_LOW 5
#define TSS_GDT_SLOT_HIGH 6
#define TSS_SELECTOR 0x28u

/* Hook in interrupts.c that lets us write the TSS descriptor into
 * its slot in the static GDT. This avoids exposing g_gdt as a
 * non-static symbol just for tss.c. */
extern void x64_gdt_write_tss_descriptor(uint64_t low_bytes,
                                          uint64_t high_bytes);

uint64_t tss_descriptor_low(uint64_t base, uint32_t limit, uint8_t dpl) {
    /* 64-bit TSS descriptor low 8 bytes layout:
     *   [15:0]   limit_low      (low 16 bits of limit)
     *   [31:16]  base_low       (low 16 bits of base)
     *   [39:32]  base_mid       (bits 23..16 of base)
     *   [47:40]  type/access    (P=1, DPL, S=0, type=0x9 = 64-bit
     *                            TSS available)
     *   [51:48]  limit_high     (bits 19..16 of limit)
     *   [55:52]  flags          (G=0, AVL=0, etc.)
     *   [63:56]  base_high      (bits 31..24 of base)
     */
    uint64_t d = 0;
    d |= (uint64_t)(limit & 0xFFFFu);
    d |= (uint64_t)(base & 0xFFFFu) << 16;
    d |= (uint64_t)((base >> 16) & 0xFFu) << 32;
    /* P=1 (bit 7), DPL (bits 5..6), S=0, type=9 (64-bit TSS avail).
     * 0x80 | (dpl << 5) | 0x09 */
    uint8_t access = (uint8_t)(0x80u | ((dpl & 0x3u) << 5) | 0x09u);
    d |= (uint64_t)access << 40;
    d |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    /* G=0, AVL=0 -> flag nibble is 0 (we use a literal limit
     * counted in bytes, not pages). */
    d |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    return d;
}

uint64_t tss_descriptor_high(uint64_t base) {
    /* 64-bit TSS descriptor high 8 bytes layout:
     *   [31:0]   base_upper     (bits 63..32 of base)
     *   [63:32]  reserved (must be 0)
     */
    return (base >> 32) & 0xFFFFFFFFull;
}

void tss_set_rsp0(uint64_t rsp0) {
    g_boot_tss.rsp0 = rsp0;
}

uint64_t tss_get_rsp0(void) {
    return g_boot_tss.rsp0;
}

void tss_init(uint64_t rsp0) {
    /* Always update RSP0 so a second call (e.g. phase 8f.2 after
     * the first user task lands) refreshes the slot without a
     * second LTR. */
    {
        uint8_t *p = (uint8_t *)&g_boot_tss;
        for (size_t i = 0; i < sizeof(g_boot_tss); ++i) p[i] = 0;
    }
    g_boot_tss.rsp0 = rsp0;
    g_boot_tss.iomap_base = (uint16_t)sizeof(struct tss);

    if (g_tss_loaded) {
        /* Second call: just refresh fields, no LTR re-issue. */
        return;
    }

    uint64_t base = (uint64_t)(uintptr_t)&g_boot_tss;
    uint32_t limit = (uint32_t)(sizeof(struct tss) - 1u);
    uint64_t low = tss_descriptor_low(base, limit, 0u);
    uint64_t high = tss_descriptor_high(base);

    x64_gdt_write_tss_descriptor(low, high);

#if defined(__x86_64__) && !defined(UNIT_TEST)
    /* Load the task register with the TSS selector. After this point
     * the CPU will swap to RSP0 on every ring 3 -> ring 0 IRQ entry. */
    __asm__ volatile("ltr %w0" : : "r"((uint16_t)TSS_SELECTOR));
#endif
    g_tss_loaded = 1;
}
