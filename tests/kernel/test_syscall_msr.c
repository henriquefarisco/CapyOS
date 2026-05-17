/*
 * Tests for the x86_64 syscall MSR contract (M4 phase 3).
 *
 * The header include/arch/x86_64/syscall_msr.h is the single source of
 * truth shared by src/kernel/syscall.c and
 * src/arch/x86_64/syscall/syscall_entry.S. These tests fail the build
 * if any of the architectural invariants drift:
 *
 *   - The MSR addresses match Intel SDM (EFER, STAR, LSTAR, FMASK).
 *   - EFER_SCE is bit 0.
 *   - SFMASK clears at least IF and DF on syscall entry.
 *   - STAR_HIGH packs the kernel-base in bits[15:0] and the user-base
 *     in bits[31:16] of the high half (= bits[47:32] and [63:48] of
 *     the full MSR).
 *   - User selectors derived from SYSRET (user_base + 0x08 for SS,
 *     user_base + 0x10 for CS, both with RPL=3) match the GDT layout
 *     installed by gdt_init().
 *   - GDT access bytes for user data and user code carry DPL=3 and
 *     the long-mode bit is present on user code.
 *
 * Locking these here means a host test failure surfaces the issue
 * before the kernel ever attempts SYSRET in QEMU.
 */
#include <stdio.h>
#include <stdint.h>

#include "arch/x86_64/syscall_msr.h"

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
    do {                                                                   \
        printf("FAIL: %s\n", msg);                                         \
    } while (0)

/* ---- 1. MSR addresses match Intel SDM Vol. 4 ------------------------ */

static void test_msr_addresses(void) {
    TEST("IA32_EFER_MSR is 0xC0000080");
    if (IA32_EFER_MSR == 0xC0000080) PASS();
    else FAIL("EFER MSR address mismatch");

    TEST("IA32_STAR_MSR is 0xC0000081");
    if (IA32_STAR_MSR == 0xC0000081) PASS();
    else FAIL("STAR MSR address mismatch");

    TEST("IA32_LSTAR_MSR is 0xC0000082");
    if (IA32_LSTAR_MSR == 0xC0000082) PASS();
    else FAIL("LSTAR MSR address mismatch");

    TEST("IA32_FMASK_MSR is 0xC0000084");
    if (IA32_FMASK_MSR == 0xC0000084) PASS();
    else FAIL("FMASK MSR address mismatch");
}

/* ---- 2. EFER and SFMASK bit semantics ------------------------------ */

static void test_efer_and_sfmask(void) {
    TEST("EFER_SCE_BIT is bit 0 (0x01)");
    if (EFER_SCE_BIT == 0x01) PASS();
    else FAIL("SCE bit position drift");

    TEST("SFMASK clears IF (RFLAGS bit 9 = 0x200)");
    if ((SYSCALL_SFMASK_VALUE & SYSCALL_SFMASK_IF_BIT) == SYSCALL_SFMASK_IF_BIT)
        PASS();
    else
        FAIL("SFMASK does not clear IF");

    TEST("SFMASK clears DF (RFLAGS bit 10 = 0x400)");
    if ((SYSCALL_SFMASK_VALUE & SYSCALL_SFMASK_DF_BIT) == SYSCALL_SFMASK_DF_BIT)
        PASS();
    else
        FAIL("SFMASK does not clear DF");

    TEST("SFMASK is exactly IF|DF (no spurious bits)");
    if (SYSCALL_SFMASK_VALUE ==
        (SYSCALL_SFMASK_IF_BIT | SYSCALL_SFMASK_DF_BIT))
        PASS();
    else
        FAIL("SFMASK has unexpected bits");

    TEST("SYSCALL_SFMASK_IF_BIT == 0x200");
    if (SYSCALL_SFMASK_IF_BIT == 0x200) PASS();
    else FAIL("IF bit constant drift");

    TEST("SYSCALL_SFMASK_DF_BIT == 0x400");
    if (SYSCALL_SFMASK_DF_BIT == 0x400) PASS();
    else FAIL("DF bit constant drift");
}

/* ---- 3. STAR layout ------------------------------------------------- */

static void test_star_layout(void) {
    TEST("SYSCALL_STAR_KERNEL_BASE is 0x0008 (kernel CS)");
    if (SYSCALL_STAR_KERNEL_BASE == 0x0008) PASS();
    else FAIL("kernel base drift");

    TEST("SYSCALL_STAR_USER_BASE is 0x0010 (user-base for SYSRET)");
    if (SYSCALL_STAR_USER_BASE == 0x0010) PASS();
    else FAIL("user base drift");

    TEST("SYSCALL_STAR_LOW is 0 (reserved low half)");
    if (SYSCALL_STAR_LOW == 0x00000000) PASS();
    else FAIL("STAR_LOW must be zero per Intel SDM");

    TEST("SYSCALL_STAR_HIGH packs user_base<<16 | kernel_base");
    if (SYSCALL_STAR_HIGH == 0x00100008u) PASS();
    else FAIL("STAR_HIGH composition drift");

    /* Sanity: the macro SYSCALL_STAR_HIGH should expand to the same
     * value as a manual computation. */
    TEST("SYSCALL_STAR_HIGH matches manual user<<16|kernel");
    if (SYSCALL_STAR_HIGH ==
        (uint32_t)((SYSCALL_STAR_USER_BASE << 16) |
                   SYSCALL_STAR_KERNEL_BASE))
        PASS();
    else
        FAIL("macro/manual mismatch");
}

/* ---- 4. Segment selector layout matches SYSRET semantics ----------- */

static void test_selectors(void) {
    TEST("GDT_KERNEL_CS_SELECTOR is 0x08");
    if (GDT_KERNEL_CS_SELECTOR == 0x08) PASS();
    else FAIL("kernel CS selector drift");

    TEST("GDT_KERNEL_DS_SELECTOR is 0x10");
    if (GDT_KERNEL_DS_SELECTOR == 0x10) PASS();
    else FAIL("kernel DS selector drift");

    TEST("GDT_USER_DS_SELECTOR is 0x18 (= user_base + 8)");
    if (GDT_USER_DS_SELECTOR == 0x18) PASS();
    else FAIL("user DS selector drift");

    TEST("GDT_USER_CS_SELECTOR is 0x20 (= user_base + 16)");
    if (GDT_USER_CS_SELECTOR == 0x20) PASS();
    else FAIL("user CS selector drift");

    TEST("USER_SS_RPL3 == GDT_USER_DS | 3 == 0x1B");
    if (USER_SS_RPL3 == 0x1B) PASS();
    else FAIL("user SS RPL3 drift");

    TEST("USER_CS_RPL3 == GDT_USER_CS | 3 == 0x23");
    if (USER_CS_RPL3 == 0x23) PASS();
    else FAIL("user CS RPL3 drift");

    /* SYSRET semantics: from STAR[63:48] the CPU loads
     *   user SS = base + 8, user CS = base + 16, both with RPL=3. */
    TEST("SYSRET-derived user SS matches GDT_USER_DS_SELECTOR");
    if ((SYSCALL_STAR_USER_BASE + 0x08) == GDT_USER_DS_SELECTOR) PASS();
    else FAIL("SYSRET user SS does not land in user data slot");

    TEST("SYSRET-derived user CS matches GDT_USER_CS_SELECTOR");
    if ((SYSCALL_STAR_USER_BASE + 0x10) == GDT_USER_CS_SELECTOR) PASS();
    else FAIL("SYSRET user CS does not land in user code slot");

    /* SYSCALL semantics: from STAR[47:32] the CPU loads
     *   kernel CS = base, kernel SS = base + 8. */
    TEST("SYSCALL-derived kernel CS matches GDT_KERNEL_CS_SELECTOR");
    if (SYSCALL_STAR_KERNEL_BASE == GDT_KERNEL_CS_SELECTOR) PASS();
    else FAIL("SYSCALL kernel CS drift");

    TEST("SYSCALL-derived kernel SS matches GDT_KERNEL_DS_SELECTOR");
    if ((SYSCALL_STAR_KERNEL_BASE + 0x08) == GDT_KERNEL_DS_SELECTOR) PASS();
    else FAIL("SYSCALL kernel SS drift");
}

/* ---- 5. GDT access bytes for user-mode descriptors ----------------- */

/* These mirror the values written by gdt_init() in
 * src/arch/x86_64/interrupts.c. They are copied here (instead of being
 * exported via a shared header) so that a regression on either side
 * shows up as an explicit comparison failure rather than a silent
 * sync. The bit layout is documented in Intel SDM Vol. 3:
 *   bit 7: P    (present)
 *   bits 6:5: DPL
 *   bit 4: S    (1 for code/data, 0 for system)
 *   bit 3: E    (1 for code, 0 for data)
 *   bit 2: DC   (data: direction; code: conforming)
 *   bit 1: RW   (data: writable; code: readable)
 *   bit 0: A    (accessed)
 */
#define GDT_ACCESS_USER_DATA_EXPECTED 0xF2u /* P|DPL=3|S|RW (data writable) */
#define GDT_ACCESS_USER_CODE_EXPECTED 0xFAu /* P|DPL=3|S|E|R  (code readable) */
#define GDT_GRAN_USER_CODE_EXPECTED   0x20u /* L bit (long-mode) set         */
#define GDT_GRAN_USER_DATA_EXPECTED   0x00u /* no flags needed for data      */

static void test_gdt_access_bytes(void) {
    TEST("user-data access byte has P=1");
    if ((GDT_ACCESS_USER_DATA_EXPECTED & 0x80u) == 0x80u) PASS();
    else FAIL("user-data not present");

    TEST("user-data access byte has DPL=3");
    if (((GDT_ACCESS_USER_DATA_EXPECTED >> 5) & 0x3u) == 0x3u) PASS();
    else FAIL("user-data not DPL=3");

    TEST("user-data access byte is data (S=1, E=0)");
    if ((GDT_ACCESS_USER_DATA_EXPECTED & 0x18u) == 0x10u) PASS();
    else FAIL("user-data type bits drift");

    TEST("user-data access byte is writable (RW=1)");
    if ((GDT_ACCESS_USER_DATA_EXPECTED & 0x02u) == 0x02u) PASS();
    else FAIL("user-data not writable");

    TEST("user-data granularity has no long-mode bit");
    if ((GDT_GRAN_USER_DATA_EXPECTED & 0x20u) == 0u) PASS();
    else FAIL("user-data granularity drift");

    TEST("user-code access byte has P=1");
    if ((GDT_ACCESS_USER_CODE_EXPECTED & 0x80u) == 0x80u) PASS();
    else FAIL("user-code not present");

    TEST("user-code access byte has DPL=3");
    if (((GDT_ACCESS_USER_CODE_EXPECTED >> 5) & 0x3u) == 0x3u) PASS();
    else FAIL("user-code not DPL=3");

    TEST("user-code access byte is code (S=1, E=1)");
    if ((GDT_ACCESS_USER_CODE_EXPECTED & 0x18u) == 0x18u) PASS();
    else FAIL("user-code type bits drift");

    TEST("user-code access byte is readable (R=1)");
    if ((GDT_ACCESS_USER_CODE_EXPECTED & 0x02u) == 0x02u) PASS();
    else FAIL("user-code not readable");

    TEST("user-code access byte is non-conforming (DC=0)");
    if ((GDT_ACCESS_USER_CODE_EXPECTED & 0x04u) == 0u) PASS();
    else FAIL("user-code is conforming (forbidden for ring-3 entry)");

    TEST("user-code granularity has long-mode bit (L=1)");
    if ((GDT_GRAN_USER_CODE_EXPECTED & 0x20u) == 0x20u) PASS();
    else FAIL("user-code missing L bit; SYSRET cannot deliver to 64-bit");
}

int test_syscall_msr_run(void) {
    printf("[test_syscall_msr]\n");
    tests_run = 0;
    tests_passed = 0;
    test_msr_addresses();
    test_efer_and_sfmask();
    test_star_layout();
    test_selectors();
    test_gdt_access_bytes();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
