/*
 * Tests for process_enter_user_mode() (M4 phase 3.5).
 *
 * Locks the C-side validator that drives the kernel-to-Ring-3
 * primitive `enter_user_mode` (defined in
 * src/arch/x86_64/cpu/user_mode_entry.S). The success path of the
 * primitive itself can only be observed in QEMU - what we lock here
 * is the contract of the validator:
 *
 *   - NULL process               -> INVALID_PROC
 *   - process without main_thread -> NO_THREAD
 *   - main_thread with rip == 0   -> BAD_RIP
 *   - main_thread with rsp == 0   -> BAD_RSP
 *   - happy path forwards (rip, rsp) to enter_user_mode and never
 *     returns past it.
 *
 * To observe the happy path on the host we ship a stub for
 * `enter_user_mode` that records its arguments and `longjmp`s back
 * to the test driver, so the marked-noreturn extern declaration in
 * src/arch/x86_64/process_user_mode.c stays honoured.
 */
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/process.h"
#include "kernel/task.h"

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

/* ---- Host stub for the asm primitive ------------------------------ */

static jmp_buf g_eum_jmp;
static int g_eum_called;
static uint64_t g_eum_seen_rip;
static uint64_t g_eum_seen_rsp;

void enter_user_mode(uint64_t rip, uint64_t rsp) __attribute__((noreturn));
void enter_user_mode(uint64_t rip, uint64_t rsp) {
    g_eum_called = 1;
    g_eum_seen_rip = rip;
    g_eum_seen_rsp = rsp;
    /* Use longjmp to satisfy the noreturn contract (control does not
     * fall through). The driver below catches the jump and inspects
     * the captured rip/rsp. */
    longjmp(g_eum_jmp, 1);
}

/* ---- Helpers ------------------------------------------------------ */

static void reset_capture(void) {
    g_eum_called = 0;
    g_eum_seen_rip = 0;
    g_eum_seen_rsp = 0;
}

static void make_process(struct process *p, struct task *t,
                        uint64_t rip, uint64_t rsp) {
    memset(p, 0, sizeof(*p));
    memset(t, 0, sizeof(*t));
    t->context.rip = rip;
    t->context.rsp = rsp;
    p->main_thread = t;
}

/* ---- 1. Validator failure paths ----------------------------------- */

static void test_validator_failures(void) {
    reset_capture();
    TEST("NULL process -> INVALID_PROC");
    if (process_enter_user_mode(NULL) ==
        PROCESS_ENTER_USER_MODE_INVALID_PROC && !g_eum_called) PASS();
    else FAIL("NULL guard or stub-call regression");

    {
        struct process p;
        memset(&p, 0, sizeof(p));
        p.main_thread = NULL;
        reset_capture();
        TEST("Process without main_thread -> NO_THREAD");
        if (process_enter_user_mode(&p) ==
            PROCESS_ENTER_USER_MODE_NO_THREAD && !g_eum_called) PASS();
        else FAIL("missing thread guard regression");
    }

    {
        struct process p; struct task t;
        make_process(&p, &t, 0, 0xCAFE0000ull);
        reset_capture();
        TEST("main_thread.rip == 0 -> BAD_RIP");
        if (process_enter_user_mode(&p) ==
            PROCESS_ENTER_USER_MODE_BAD_RIP && !g_eum_called) PASS();
        else FAIL("rip == 0 leaked to enter_user_mode");
    }

    {
        struct process p; struct task t;
        make_process(&p, &t, 0xBABE0000ull, 0);
        reset_capture();
        TEST("main_thread.rsp == 0 -> BAD_RSP");
        if (process_enter_user_mode(&p) ==
            PROCESS_ENTER_USER_MODE_BAD_RSP && !g_eum_called) PASS();
        else FAIL("rsp == 0 leaked to enter_user_mode");
    }
}

/* ---- 2. Validator forwards rip/rsp to the asm primitive ----------- */

static void test_validator_happy_path(void) {
    struct process p; struct task t;
    make_process(&p, &t, 0x4000B0DEull, 0x7FFEFFE8ull);
    reset_capture();

    TEST("happy path forwards rip/rsp to enter_user_mode");
    if (setjmp(g_eum_jmp) == 0) {
        /* This call should not return - the stub longjmps back. */
        (void)process_enter_user_mode(&p);
        FAIL("validator returned past enter_user_mode (noreturn broken)");
        return;
    }
    /* setjmp returned 1 = stub fired. */
    if (g_eum_called &&
        g_eum_seen_rip == 0x4000B0DEull &&
        g_eum_seen_rsp == 0x7FFEFFE8ull) PASS();
    else FAIL("rip/rsp were not forwarded verbatim");
}

/* ---- 3. Enum value contract --------------------------------------- */

static void test_enum_contract(void) {
    TEST("PROCESS_ENTER_USER_MODE_OK == 0");
    if (PROCESS_ENTER_USER_MODE_OK == 0) PASS();
    else FAIL("OK enum drift");

    TEST("INVALID_PROC == -1, NO_THREAD == -2, BAD_RIP == -3, BAD_RSP == -4");
    if (PROCESS_ENTER_USER_MODE_INVALID_PROC == -1 &&
        PROCESS_ENTER_USER_MODE_NO_THREAD    == -2 &&
        PROCESS_ENTER_USER_MODE_BAD_RIP      == -3 &&
        PROCESS_ENTER_USER_MODE_BAD_RSP      == -4) PASS();
    else FAIL("error enum value drift");
}

int test_enter_user_mode_run(void) {
    printf("[test_enter_user_mode]\n");
    tests_run = 0;
    tests_passed = 0;
    test_validator_failures();
    test_validator_happy_path();
    test_enum_contract();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
