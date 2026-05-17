#include "kernel/linux_compat/linux_mlock.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* --- mlock / munlock --- */

static void t_mlock_zero_len(void) {
    TEST("mlock(addr, 0) -> 0 (Linux short-circuit)");
    if (linux_mlock(0x1000, 0) == 0) PASS();
    else FAIL("zero len not zero");
}

static void t_mlock_basic(void) {
    TEST("mlock(0x1000, 0x1000) -> 0 (no swap; pages pinned)");
    if (linux_mlock(0x1000, 0x1000) == 0) PASS();
    else FAIL("basic mlock failed");
}

static void t_mlock_overflow_einval(void) {
    TEST("mlock with addr+len wrap -> -EINVAL");
    if (linux_mlock(UINT64_MAX - 100, 1024) == -LINUX_EINVAL) PASS();
    else FAIL("overflow not surfaced");
}

static void t_munlock_basic(void) {
    TEST("munlock(0x1000, 0x1000) -> 0");
    if (linux_munlock(0x1000, 0x1000) == 0) PASS();
    else FAIL("munlock failed");
}

static void t_munlock_overflow_einval(void) {
    TEST("munlock with addr+len wrap -> -EINVAL");
    if (linux_munlock(UINT64_MAX - 100, 1024) == -LINUX_EINVAL) PASS();
    else FAIL("overflow not surfaced");
}

/* --- mlockall / munlockall --- */

static void t_mlockall_zero_einval(void) {
    TEST("mlockall(0) -> -EINVAL (Linux requires at least one bit)");
    if (linux_mlockall(0) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_mlockall_unknown_einval(void) {
    TEST("mlockall(0xDEAD) -> -EINVAL");
    if (linux_mlockall(0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_mlockall_current_ok(void) {
    TEST("mlockall(MCL_CURRENT) -> 0");
    if (linux_mlockall(LINUX_MCL_CURRENT) == 0) PASS();
    else FAIL("MCL_CURRENT rejected");
}

static void t_mlockall_current_future_onfault_ok(void) {
    TEST("mlockall(MCL_CURRENT|FUTURE|ONFAULT) -> 0");
    int flags = LINUX_MCL_CURRENT | LINUX_MCL_FUTURE | LINUX_MCL_ONFAULT;
    if (linux_mlockall(flags) == 0) PASS();
    else FAIL("combined flags rejected");
}

static void t_munlockall_ok(void) {
    TEST("munlockall() -> 0");
    if (linux_munlockall() == 0) PASS();
    else FAIL("munlockall failed");
}

int test_linux_mlock_run(void) {
    printf("[test_linux_mlock]\n");
    tests_run = tests_passed = 0;

    t_mlock_zero_len();
    t_mlock_basic();
    t_mlock_overflow_einval();
    t_munlock_basic();
    t_munlock_overflow_einval();

    t_mlockall_zero_einval();
    t_mlockall_unknown_einval();
    t_mlockall_current_ok();
    t_mlockall_current_future_onfault_ok();
    t_munlockall_ok();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
