/*
 * Host tests for arch_fault_classify() (M4 phase 4).
 *
 * Locks the full classification matrix that the x86_64 exception
 * dispatcher relies on, so any future drift (renaming an enum value,
 * dropping a vector, changing the CPL boundary) trips a host test
 * before it can leak into a kernel build.
 *
 * Pure-function tests: no kernel state, no inline asm, no IDT setup.
 * Just the contract documented in include/arch/x86_64/fault_classify.h.
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/fault_classify.h"

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

/* Selectors mirroring include/arch/x86_64/syscall_msr.h. We re-spell
 * them rather than including syscall_msr.h so this test fails for
 * exactly its own reason: a regression in the classifier, not in the
 * MSR header. */
#define KERNEL_CS 0x08u
#define KERNEL_DS 0x10u
#define USER_DS_RPL3 0x1Bu
#define USER_CS_RPL3 0x23u

static struct arch_fault_info make_info(uint64_t vector, uint64_t cs) {
    struct arch_fault_info info = {
        .vector = vector,
        .error_code = 0,
        .cs = cs,
        .rip = 0xCAFEBABEull,
        .cr2 = 0,
    };
    return info;
}

/* ---- 1. arch_fault_is_user CPL boundary ------------------------------ */

static void test_is_user_cpl_boundary(void) {
    TEST("arch_fault_is_user: kernel CS (RPL=0) -> 0");
    if (arch_fault_is_user(KERNEL_CS) == 0) PASS();
    else FAIL("kernel CS reported as user");

    TEST("arch_fault_is_user: kernel DS (RPL=0) -> 0");
    if (arch_fault_is_user(KERNEL_DS) == 0) PASS();
    else FAIL("kernel DS reported as user");

    TEST("arch_fault_is_user: user CS RPL=3 -> 1");
    if (arch_fault_is_user(USER_CS_RPL3) == 1) PASS();
    else FAIL("user CS not detected");

    TEST("arch_fault_is_user: user DS RPL=3 -> 1");
    if (arch_fault_is_user(USER_DS_RPL3) == 1) PASS();
    else FAIL("user DS not detected");

    TEST("arch_fault_is_user: RPL=1 (not user)");
    if (arch_fault_is_user(0x09u) == 0) PASS();
    else FAIL("RPL=1 leaked as user");

    TEST("arch_fault_is_user: RPL=2 (not user)");
    if (arch_fault_is_user(0x0Au) == 0) PASS();
    else FAIL("RPL=2 leaked as user");

    TEST("arch_fault_is_user: high selector bits ignored");
    if (arch_fault_is_user(0xFFFFu | 0x3u) == 1) PASS();
    else FAIL("high bits affected RPL check");
}

/* ---- 2. NULL guard -------------------------------------------------- */

static void test_null_guard(void) {
    TEST("arch_fault_classify: NULL info -> panic");
    if (arch_fault_classify(NULL) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("NULL info classifier did not default to panic");
}

/* ---- 3. Kernel-mode faults always panic ----------------------------- */

static void test_kernel_faults_panic(void) {
    struct arch_fault_info kpf = make_info(14, KERNEL_CS);
    TEST("kernel #PF (vec 14) -> panic");
    if (arch_fault_classify(&kpf) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("kernel #PF did not panic");

    struct arch_fault_info kgp = make_info(13, KERNEL_CS);
    TEST("kernel #GP (vec 13) -> panic");
    if (arch_fault_classify(&kgp) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("kernel #GP did not panic");

    struct arch_fault_info kud = make_info(6, KERNEL_CS);
    TEST("kernel #UD (vec 6) -> panic");
    if (arch_fault_classify(&kud) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("kernel #UD did not panic");

    struct arch_fault_info kde = make_info(0, KERNEL_CS);
    TEST("kernel #DE (vec 0) -> panic");
    if (arch_fault_classify(&kde) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("kernel #DE did not panic");
}

/* ---- 4. Platform-fatal vectors panic regardless of CPL ------------- */

static void test_platform_fatal(void) {
    struct arch_fault_info nmi_user = make_info(2, USER_CS_RPL3);
    TEST("NMI from user -> panic");
    if (arch_fault_classify(&nmi_user) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("NMI from user did not panic");

    struct arch_fault_info nmi_kern = make_info(2, KERNEL_CS);
    TEST("NMI from kernel -> panic");
    if (arch_fault_classify(&nmi_kern) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("NMI from kernel did not panic");

    struct arch_fault_info df_user = make_info(8, USER_CS_RPL3);
    TEST("#DF from user -> panic");
    if (arch_fault_classify(&df_user) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("#DF from user did not panic");

    struct arch_fault_info df_kern = make_info(8, KERNEL_CS);
    TEST("#DF from kernel -> panic");
    if (arch_fault_classify(&df_kern) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("#DF from kernel did not panic");

    struct arch_fault_info mc_user = make_info(18, USER_CS_RPL3);
    TEST("#MC from user -> panic");
    if (arch_fault_classify(&mc_user) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("#MC from user did not panic");

    struct arch_fault_info mc_kern = make_info(18, KERNEL_CS);
    TEST("#MC from kernel -> panic");
    if (arch_fault_classify(&mc_kern) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("#MC from kernel did not panic");
}

/* ---- 5. User-mode recoverable vectors map to kill ------------------ */

static const struct {
    uint64_t vector;
    const char *label;
} kill_table[] = {
    {  0, "user #DE (vec 0)  -> kill" },
    {  1, "user #DB (vec 1)  -> kill" },
    {  3, "user #BP (vec 3)  -> kill" },
    {  4, "user #OF (vec 4)  -> kill" },
    {  5, "user #BR (vec 5)  -> kill" },
    {  6, "user #UD (vec 6)  -> kill" },
    {  7, "user #NM (vec 7)  -> kill" },
    { 10, "user #TS (vec 10) -> kill" },
    { 11, "user #NP (vec 11) -> kill" },
    { 12, "user #SS (vec 12) -> kill" },
    { 13, "user #GP (vec 13) -> kill" },
    /* user #PF (vector 14) is now classified by error code: see
     * test_pf_recoverable / test_pf_kill below. The plain make_info()
     * default of error_code=0 means "not-present read", which maps to
     * RECOVERABLE post-phase-7a, so the entry was removed from this
     * vector-only table to keep the test for what it claims to cover. */
    { 16, "user #MF (vec 16) -> kill" },
    { 17, "user #AC (vec 17) -> kill" },
    { 19, "user #XM (vec 19) -> kill" },
    { 20, "user #VE (vec 20) -> kill" },
    { 21, "user #CP (vec 21) -> kill" },
};

static void test_user_recoverable_kill(void) {
    for (size_t i = 0; i < sizeof(kill_table) / sizeof(kill_table[0]); ++i) {
        struct arch_fault_info ui = make_info(kill_table[i].vector,
                                              USER_CS_RPL3);
        TEST(kill_table[i].label);
        if (arch_fault_classify(&ui) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("user fault did not map to kill");
    }
}

/* ---- 6. Reserved / unknown user-mode vectors panic ------------------ */

static void test_user_reserved_panic(void) {
    struct arch_fault_info v9 = make_info(9, USER_CS_RPL3);
    TEST("user vector 9 (reserved) -> panic");
    if (arch_fault_classify(&v9) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("reserved vector 9 leaked to kill");

    struct arch_fault_info v15 = make_info(15, USER_CS_RPL3);
    TEST("user vector 15 (reserved) -> panic");
    if (arch_fault_classify(&v15) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("reserved vector 15 leaked to kill");

    struct arch_fault_info v22 = make_info(22, USER_CS_RPL3);
    TEST("user vector 22 (reserved) -> panic");
    if (arch_fault_classify(&v22) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("reserved vector 22 leaked to kill");

    struct arch_fault_info v31 = make_info(31, USER_CS_RPL3);
    TEST("user vector 31 (reserved) -> panic");
    if (arch_fault_classify(&v31) == ARCH_FAULT_KERNEL_PANIC) PASS();
    else FAIL("reserved vector 31 leaked to kill");
}

/* ---- 7. User-mode #PF error-code matrix (phase 7a) ---------------- */

/* Page-fault error code bits, kept in sync with src/arch/x86_64/
 * fault_classify.c. Re-spelled locally so a regression there does not
 * silently desynchronise the test expectations. */
#define PF_P    (1u << 0)  /* 1 = protection violation, 0 = not present */
#define PF_W    (1u << 1)  /* 1 = write access */
#define PF_U    (1u << 2)  /* 1 = user mode */
#define PF_RSVD (1u << 3)  /* 1 = reserved bit set in paging structure */
#define PF_I    (1u << 4)  /* 1 = instruction fetch */
#define PF_PK   (1u << 5)  /* 1 = protection-key violation */

static struct arch_fault_info make_pf(uint64_t error_code, uint64_t cs) {
    struct arch_fault_info info = make_info(14, cs);
    info.error_code = error_code;
    info.cr2 = 0xDEADC0DEull;
    return info;
}

static void test_pf_recoverable(void) {
    /* Not-present read from user (lazy heap, lazy mmap). */
    {
        struct arch_fault_info pf = make_pf(PF_U, USER_CS_RPL3);
        TEST("user #PF U=1 P=0 W=0 -> RECOVERABLE");
        if (arch_fault_classify(&pf) == ARCH_FAULT_RECOVERABLE) PASS();
        else FAIL("not-present read user #PF did not map to RECOVERABLE");
    }
    /* Not-present write from user (heap growth, sbrk-style). */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_W, USER_CS_RPL3);
        TEST("user #PF U=1 P=0 W=1 -> RECOVERABLE");
        if (arch_fault_classify(&pf) == ARCH_FAULT_RECOVERABLE) PASS();
        else FAIL("not-present write user #PF did not map to RECOVERABLE");
    }
    /* Not-present instruction fetch (lazy code mapping). */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_I, USER_CS_RPL3);
        TEST("user #PF U=1 P=0 I=1 -> RECOVERABLE");
        if (arch_fault_classify(&pf) == ARCH_FAULT_RECOVERABLE) PASS();
        else FAIL("not-present ifetch user #PF did not map to RECOVERABLE");
    }
}

static void test_pf_kill(void) {
    /* Protection violation read (e.g. user touching a kernel page).
     * Today this is NOT recoverable: there is no legitimate user
     * scenario in which a present-page read should fault. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_P, USER_CS_RPL3);
        TEST("user #PF U=1 P=1 W=0 -> KILL_PROCESS");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("protection violation read did not kill");
    }
    /* Protection violation read on present page. No legitimate user
     * scenario asks the VMM to recover this: text segments and
     * mprotect-RO mappings are explicitly read-only and reads on
     * them never fault, so a #PF here means the user touched
     * something it shouldn't. Stays KILL post-phase-7c. */
    /* (covered by the U=1 P=1 W=0 test just above) */
    /* Reserved bit set: page table is corrupt, cannot recover safely. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_RSVD, USER_CS_RPL3);
        TEST("user #PF RSVD=1 -> KILL_PROCESS");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("reserved bit set did not kill");
    }
    /* Protection-key violation: PKRU policy denial. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_PK, USER_CS_RPL3);
        TEST("user #PF PK=1 -> KILL_PROCESS");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("protection key violation did not kill");
    }
}

static void test_pf_kernel_always_panics(void) {
    /* Kernel-mode #PF panics regardless of error-code shape. The kernel
     * does not demand-page its own working set today, so a fault here
     * means a real bug (stale pointer, unmapped MMIO, etc.). */
    {
        struct arch_fault_info pf = make_pf(0u, KERNEL_CS);
        TEST("kernel #PF P=0 W=0 -> PANIC");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KERNEL_PANIC) PASS();
        else FAIL("kernel #PF leaked recoverable");
    }
    {
        struct arch_fault_info pf = make_pf(PF_W, KERNEL_CS);
        TEST("kernel #PF P=0 W=1 -> PANIC");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KERNEL_PANIC) PASS();
        else FAIL("kernel write #PF leaked recoverable");
    }
}

/* ---- 7c. CoW candidates (M4 phase 7c) ------------------------------ */

static void test_pf_cow_candidate(void) {
    /* Phase 7c: a write on a present page from user mode is now a
     * RECOVERABLE candidate at the classifier level. The VMM body
     * inspects the PTE's software COW bit and either reuses-in-place
     * or allocates a copy; if the PTE is genuinely RO (text, mprotect
     * RO) the VMM returns -1 and the dispatcher escalates back to
     * KILL_PROCESS. The classifier no longer pre-judges that case. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_P | PF_W,
                                            USER_CS_RPL3);
        TEST("user #PF U=1 P=1 W=1 -> RECOVERABLE (CoW candidate)");
        if (arch_fault_classify(&pf) == ARCH_FAULT_RECOVERABLE) PASS();
        else FAIL("CoW candidate not flagged recoverable");
    }
    /* RSVD trumps the CoW candidate shape: corrupt page table is
     * never recoverable regardless of P/W. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_P | PF_W | PF_RSVD,
                                            USER_CS_RPL3);
        TEST("user #PF P=1 W=1 RSVD=1 -> KILL_PROCESS");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("RSVD did not override CoW candidate");
    }
    /* PK trumps CoW too. */
    {
        struct arch_fault_info pf = make_pf(PF_U | PF_P | PF_W | PF_PK,
                                            USER_CS_RPL3);
        TEST("user #PF P=1 W=1 PK=1 -> KILL_PROCESS");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KILL_PROCESS) PASS();
        else FAIL("PK did not override CoW candidate");
    }
    /* Kernel-mode write-on-present #PF is still a kernel bug;
     * phase 7c does not extend the user-only RECOVERABLE arm
     * to ring 0. */
    {
        struct arch_fault_info pf = make_pf(PF_P | PF_W, KERNEL_CS);
        TEST("kernel #PF P=1 W=1 -> PANIC (CoW does not apply)");
        if (arch_fault_classify(&pf) == ARCH_FAULT_KERNEL_PANIC) PASS();
        else FAIL("kernel CoW shape leaked recoverable");
    }
}

/* ---- 8. Enum value contract ---------------------------------------- */

static void test_enum_contract(void) {
    TEST("ARCH_FAULT_KERNEL_PANIC == 0");
    if (ARCH_FAULT_KERNEL_PANIC == 0) PASS();
    else FAIL("KERNEL_PANIC enum value drift");

    TEST("ARCH_FAULT_KILL_PROCESS == 1");
    if (ARCH_FAULT_KILL_PROCESS == 1) PASS();
    else FAIL("KILL_PROCESS enum value drift");

    TEST("ARCH_FAULT_RECOVERABLE == 2");
    if (ARCH_FAULT_RECOVERABLE == 2) PASS();
    else FAIL("RECOVERABLE enum value drift");
}

int test_fault_classify_run(void) {
    printf("[test_fault_classify]\n");
    tests_run = 0;
    tests_passed = 0;
    test_is_user_cpl_boundary();
    test_null_guard();
    test_kernel_faults_panic();
    test_platform_fatal();
    test_user_recoverable_kill();
    test_user_reserved_panic();
    test_pf_recoverable();
    test_pf_kill();
    test_pf_cow_candidate();
    test_pf_kernel_always_panics();
    test_enum_contract();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
