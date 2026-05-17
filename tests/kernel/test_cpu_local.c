/*
 * Tests for the per-CPU area contract used by the syscall path
 * (M4 phase 3.5).
 *
 * Lock the layout that include/arch/x86_64/cpu_local.h advertises:
 *   - struct cpu_local member offsets match the asm constants used in
 *     src/arch/x86_64/syscall/syscall_entry.S
 *     (CPU_LOCAL_KERNEL_RSP_OFFSET, CPU_LOCAL_USER_RSP_SCRATCH_OFFSET).
 *   - sizeof(struct cpu_local) == CPU_LOCAL_SIZE so a future field
 *     addition is forced to update the constant.
 *   - The Intel SDM MSR addresses match what we expect (IA32_GS_BASE,
 *     IA32_KERNEL_GS_BASE).
 *   - cpu_local_init() updates the kernel_rsp slot, sets the
 *     initialized flag, and is idempotent.
 *
 * No host execution of the wrmsr is required: cpu_local.c #ifdefs
 * the inline asm so on UNIT_TEST the wrmsr64 helper is a no-op stub.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "arch/x86_64/cpu_local.h"

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

/* ---- 1. Layout constants match the C struct ----------------------- */

static void test_layout_offsets(void) {
    TEST("CPU_LOCAL_KERNEL_RSP_OFFSET == 0x00");
    if (CPU_LOCAL_KERNEL_RSP_OFFSET == 0x00) PASS();
    else FAIL("kernel_rsp offset constant drift");

    TEST("CPU_LOCAL_USER_RSP_SCRATCH_OFFSET == 0x08");
    if (CPU_LOCAL_USER_RSP_SCRATCH_OFFSET == 0x08) PASS();
    else FAIL("user_rsp_scratch offset constant drift");

    TEST("CPU_LOCAL_SIZE == 0x10");
    if (CPU_LOCAL_SIZE == 0x10) PASS();
    else FAIL("cpu_local size constant drift");

    TEST("offsetof(cpu_local, kernel_rsp) matches constant");
    if (offsetof(struct cpu_local, kernel_rsp) ==
        CPU_LOCAL_KERNEL_RSP_OFFSET) PASS();
    else FAIL("struct member offset drifted from asm constant");

    TEST("offsetof(cpu_local, user_rsp_scratch) matches constant");
    if (offsetof(struct cpu_local, user_rsp_scratch) ==
        CPU_LOCAL_USER_RSP_SCRATCH_OFFSET) PASS();
    else FAIL("struct member offset drifted from asm constant");

    TEST("sizeof(struct cpu_local) matches CPU_LOCAL_SIZE");
    if (sizeof(struct cpu_local) == CPU_LOCAL_SIZE) PASS();
    else FAIL("struct size drifted from asm constant");
}

/* ---- 2. MSR addresses match Intel SDM ----------------------------- */

static void test_msr_addresses(void) {
    TEST("IA32_GS_BASE_MSR == 0xC0000101");
    if (IA32_GS_BASE_MSR == 0xC0000101u) PASS();
    else FAIL("IA32_GS_BASE address drift");

    TEST("IA32_KERNEL_GS_BASE_MSR == 0xC0000102");
    if (IA32_KERNEL_GS_BASE_MSR == 0xC0000102u) PASS();
    else FAIL("IA32_KERNEL_GS_BASE address drift");
}

/* ---- 3. cpu_local_init contract ----------------------------------- */

static void test_init_contract(void) {
    TEST("cpu_local_init stores kernel_rsp");
    cpu_local_init(0xDEADBEEF12345000ull);
    if (cpu_local_get_kernel_rsp() == 0xDEADBEEF12345000ull) PASS();
    else FAIL("kernel_rsp not written by cpu_local_init");

    TEST("cpu_local_is_initialized after first init");
    if (cpu_local_is_initialized() == 1) PASS();
    else FAIL("init flag did not flip");

    TEST("cpu_local_set_kernel_rsp updates the slot");
    cpu_local_set_kernel_rsp(0xCAFEBABE00000000ull);
    if (cpu_local_get_kernel_rsp() == 0xCAFEBABE00000000ull) PASS();
    else FAIL("kernel_rsp setter ignored");

    TEST("cpu_local_init is idempotent (does not flip flag back)");
    cpu_local_init(0x1111222233334444ull);
    if (cpu_local_is_initialized() == 1 &&
        cpu_local_get_kernel_rsp() == 0x1111222233334444ull) PASS();
    else FAIL("idempotent init regressed");
}

int test_cpu_local_run(void) {
    printf("[test_cpu_local]\n");
    tests_run = 0;
    tests_passed = 0;
    test_layout_offsets();
    test_msr_addresses();
    test_init_contract();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
