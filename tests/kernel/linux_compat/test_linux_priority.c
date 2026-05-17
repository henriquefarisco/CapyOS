#include "kernel/linux_compat/linux_priority.h"
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

static void t_default_nice_zero_encoded_20(void) {
    linux_priority_reset_for_tests();
    TEST("getpriority(PROCESS, 0) -> 20 (encoded nice=0)");
    if (linux_getpriority(LINUX_PRIO_PROCESS, 0) == 20) PASS();
    else FAIL("default not 20");
}

static void t_invalid_which_einval(void) {
    TEST("getpriority(99, 0) -> -EINVAL");
    if (linux_getpriority(99, 0) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_set_invalid_which_einval(void) {
    TEST("setpriority(99, 0, 5) -> -EINVAL");
    if (linux_setpriority(99, 0, 5) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_set_get_roundtrip(void) {
    linux_priority_reset_for_tests();
    int64_t r = linux_setpriority(LINUX_PRIO_PROCESS, 0, 10);
    int64_t g = linux_getpriority(LINUX_PRIO_PROCESS, 0);
    TEST("setpriority(10) then getpriority -> 10 (encoded = 20-10 = 10)");
    if (r == 0 && g == 10) PASS();
    else FAIL("roundtrip wrong");
}

static void t_set_negative_nice_encoded_high(void) {
    linux_priority_reset_for_tests();
    (void)linux_setpriority(LINUX_PRIO_PROCESS, 0, -20);
    TEST("setpriority(-20) -> getpriority returns 40 (encoded = 20-(-20))");
    if (linux_getpriority(LINUX_PRIO_PROCESS, 0) == 40) PASS();
    else FAIL("negative nice encoding wrong");
}

static void t_set_clamps_above_max(void) {
    linux_priority_reset_for_tests();
    (void)linux_setpriority(LINUX_PRIO_PROCESS, 0, 100);
    TEST("setpriority(100) clamps to NICE_MAX(19) -> encoded = 1");
    if (linux_getpriority(LINUX_PRIO_PROCESS, 0) == 1) PASS();
    else FAIL("clamp wrong");
}

static void t_set_clamps_below_min(void) {
    linux_priority_reset_for_tests();
    (void)linux_setpriority(LINUX_PRIO_PROCESS, 0, -100);
    TEST("setpriority(-100) clamps to NICE_MIN(-20) -> encoded = 40");
    if (linux_getpriority(LINUX_PRIO_PROCESS, 0) == 40) PASS();
    else FAIL("clamp wrong");
}

static void t_pgrp_user_accepted(void) {
    linux_priority_reset_for_tests();
    int ok_pgrp = linux_getpriority(LINUX_PRIO_PGRP, 0) == 20;
    int ok_user = linux_getpriority(LINUX_PRIO_USER, 0) == 20;
    TEST("getpriority(PGRP) and (USER) accepted (-> 20 default)");
    if (ok_pgrp && ok_user) PASS();
    else FAIL("PGRP/USER not accepted");
}

int test_linux_priority_run(void) {
    printf("[test_linux_priority]\n");
    tests_run = tests_passed = 0;

    t_default_nice_zero_encoded_20();
    t_invalid_which_einval();
    t_set_invalid_which_einval();
    t_set_get_roundtrip();
    t_set_negative_nice_encoded_high();
    t_set_clamps_above_max();
    t_set_clamps_below_min();
    t_pgrp_user_accepted();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
