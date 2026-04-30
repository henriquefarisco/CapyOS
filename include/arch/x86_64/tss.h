#ifndef ARCH_X86_64_TSS_H
#define ARCH_X86_64_TSS_H

#include <stdint.h>

/*
 * x86_64 Task State Segment (M4 phase 8f.1).
 *
 * The 64-bit TSS does NOT do hardware task switching - that's a
 * 32-bit-protected-mode legacy feature that long mode dropped.
 * What the 64-bit TSS DOES provide, and why CapyOS finally needs
 * one, is the **RSP0 / RSP1 / RSP2 stack pointer fields** the CPU
 * loads when an interrupt or exception transfers control from a
 * less-privileged ring to a more-privileged ring.
 *
 * Without a TSS loaded via LTR:
 *   - SYSCALL works (it does its own stack swap via cpu_local /
 *     IA32_GS_BASE, see syscall_entry.S), so phase 5e/5f hello
 *     smokes work today.
 *   - INT/IRQ from ring 3 -> ring 0 has no defined kernel stack and
 *     the CPU raises #DF (double fault) the first time the APIC
 *     tick fires while the user binary is in ring 3.
 *
 * Phase 8f.1 establishes the TSS scaffolding so ring-3 IRQs are
 * actually safe; phase 8f.2 will hook context_switch to swap RSP0
 * to the active task's per-task kernel stack so multiple ring-3
 * tasks can share the IRQ handler without clobbering each other's
 * IRQ frames.
 *
 * The struct layout below is dictated by hardware (Intel SDM Vol 3
 * Section 7.7) and is locked by host tests in
 * tests/test_tss_layout.c. iomap_base is set to sizeof(struct tss)
 * which means "no I/O permission bitmap"; the CPU treats every I/O
 * port as denied for ring-3 code (correct default).
 */

struct __attribute__((packed)) tss {
    uint32_t reserved1;
    uint64_t rsp0;          /* stack on transition to ring 0 */
    uint64_t rsp1;          /* stack on transition to ring 1 (unused) */
    uint64_t rsp2;          /* stack on transition to ring 2 (unused) */
    uint64_t reserved2;
    uint64_t ist1;          /* IST entries (unused; we use RSP0) */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;    /* offset to I/O permission bitmap */
};

#define TSS_SIZE 104
#define TSS_RSP0_OFFSET 0x04u
#define TSS_IOMAP_BASE_OFFSET 0x66u

/*
 * Initialise the boot TSS:
 *   1. Zero all fields.
 *   2. Set RSP0 to the supplied kernel stack top.
 *   3. Set iomap_base = sizeof(struct tss) so I/O is denied.
 *   4. Write the 16-byte 64-bit TSS descriptor into the GDT slot
 *      identified by `tss_gdt_descriptor_phys` (low 8 bytes) and
 *      `tss_gdt_descriptor_phys + 8` (high 8 bytes). The selector
 *      to LTR is `tss_selector`.
 *   5. Issue `LTR` with the supplied selector.
 *
 * The function is idempotent in the sense that calling it again
 * with a different rsp0 will simply rewrite the RSP0 slot; the LTR
 * step is performed only once (the second call sees an already-
 * loaded TR and returns early).
 *
 * tss_descriptor_low_bytes / tss_descriptor_high_bytes are exposed
 * separately so tests can verify the encoding without driving LTR
 * (which only makes sense on real x86_64 hardware).
 */
void tss_init(uint64_t rsp0);

/* Update the active TSS's RSP0 field. Used by phase 8f.2 from the
 * scheduler when switching between user tasks so each task's IRQs
 * land on its own per-task kernel stack. Safe to call before
 * tss_init() (becomes a no-op). */
void tss_set_rsp0(uint64_t rsp0);

/* Read back the active TSS's RSP0 (test/observability). Returns 0
 * if the TSS has not been initialised yet. */
uint64_t tss_get_rsp0(void);

/* Encode the low 8 bytes of a 64-bit TSS GDT descriptor pointing
 * at `base` with `limit` bytes. `dpl` is the privilege level
 * (always 0 for kernel TSS). Pure function for host tests. */
uint64_t tss_descriptor_low(uint64_t base, uint32_t limit, uint8_t dpl);
/* Encode the high 8 bytes of a 64-bit TSS GDT descriptor. */
uint64_t tss_descriptor_high(uint64_t base);

#endif /* ARCH_X86_64_TSS_H */
