/*
 * x86_64 syscall MSR contract for CapyOS.
 *
 * This header is the single source of truth for the MSR addresses,
 * EFER bit, SFMASK value, STAR layout and segment selectors that the
 * kernel uses to wire up SYSCALL/SYSRET. Both the C side
 * (src/kernel/syscall.c via syscall_init) and the assembly side
 * (src/arch/x86_64/syscall/syscall_entry.S) must agree on these
 * constants; tests/test_syscall_msr.c locks the values so a
 * silent drift cannot ship.
 *
 * Intel SDM references:
 *   - Vol. 2: SYSCALL / SYSRET semantics.
 *   - Vol. 4: IA32_EFER, IA32_STAR, IA32_LSTAR, IA32_FMASK MSR layout.
 *
 * The comments here are the canonical description of how SYSRET
 * derives user CS and SS from STAR[63:48] in 64-bit mode:
 *   user CS = STAR[63:48] + 0x10, with DPL=3 (i.e. base + 0x13)
 *   user SS = STAR[63:48] + 0x08, with DPL=3 (i.e. base + 0x0B)
 * For SYSCALL the CPU loads:
 *   kernel CS = STAR[47:32]
 *   kernel SS = STAR[47:32] + 0x08
 *
 * The header is included from both .c and .S sources. Constants are
 * expressed in a way that the GNU assembler accepts directly; no C
 * statements are introduced when included from .S.
 */
#ifndef ARCH_X86_64_SYSCALL_MSR_H
#define ARCH_X86_64_SYSCALL_MSR_H

/* ---- IA32 MSR addresses ---------------------------------------------- */
#define IA32_EFER_MSR    0xC0000080
#define IA32_STAR_MSR    0xC0000081
#define IA32_LSTAR_MSR   0xC0000082
#define IA32_FMASK_MSR   0xC0000084

/* ---- EFER bits ------------------------------------------------------- */
#define EFER_SCE_BIT     0x01

/* ---- SFMASK bits cleared on SYSCALL entry --------------------------- */
/* Clearing IF disables interrupts on syscall entry until the kernel
 * has switched to a kernel stack and saved the user state. Clearing
 * DF guarantees the system V calling convention's expectation that
 * DF=0 on entry to library code (and avoids subtle string-op bugs). */
#define SYSCALL_SFMASK_IF_BIT 0x200
#define SYSCALL_SFMASK_DF_BIT 0x400
#define SYSCALL_SFMASK_VALUE  (SYSCALL_SFMASK_IF_BIT | SYSCALL_SFMASK_DF_BIT)

/* ---- GDT segment selectors ------------------------------------------ */
/* The kernel GDT layout (see src/arch/x86_64/interrupts.c::gdt_init):
 *   index 0 (selector 0x00) : null
 *   index 1 (selector 0x08) : kernel code   (DPL=0)
 *   index 2 (selector 0x10) : kernel data   (DPL=0)
 *   index 3 (selector 0x18) : user data     (DPL=3, RPL=3 -> 0x1B)
 *   index 4 (selector 0x20) : user code     (DPL=3, RPL=3 -> 0x23)
 *
 * The selector ordering for user descriptors is dictated by SYSRET:
 * STAR[63:48] is the base. With base=0x10, SYSRET derives
 *   user SS = base + 8 = 0x18 (RPL=3 -> 0x1B)
 *   user CS = base + 16 = 0x20 (RPL=3 -> 0x23)
 * which is why the user data slot precedes the user code slot in
 * the GDT, even though that is the opposite of how the kernel slots
 * are ordered. */
#define GDT_KERNEL_CS_SELECTOR  0x08
#define GDT_KERNEL_DS_SELECTOR  0x10
#define GDT_USER_DS_SELECTOR    0x18
#define GDT_USER_CS_SELECTOR    0x20

#define USER_CS_RPL3            (GDT_USER_CS_SELECTOR | 0x3) /* 0x23 */
#define USER_SS_RPL3            (GDT_USER_DS_SELECTOR | 0x3) /* 0x1B */

/* ---- STAR MSR composition ------------------------------------------- */
/* STAR is a 64-bit MSR loaded as (high32 << 32) | low32. The high
 * 32 bits hold the SYSCALL kernel CS in bits[47:32] and the SYSRET
 * user-base CS in bits[63:48]. */
#define SYSCALL_STAR_KERNEL_BASE 0x0008  /* must equal GDT_KERNEL_CS_SELECTOR */
#define SYSCALL_STAR_USER_BASE   0x0010  /* SYSRET base: user SS = base+8, user CS = base+16 */

#define SYSCALL_STAR_LOW         0x00000000
#define SYSCALL_STAR_HIGH \
    ((SYSCALL_STAR_USER_BASE << 16) | SYSCALL_STAR_KERNEL_BASE)
/* For SYSCALL_STAR_USER_BASE = 0x10 and SYSCALL_STAR_KERNEL_BASE = 0x08
 * this resolves to 0x00100008. */

#endif /* ARCH_X86_64_SYSCALL_MSR_H */
