#include "kernel/linux_compat/linux_umask.h"

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

static void t_default_is_022(void) {
    linux_umask_reset_for_tests();
    /* Reading default: set then read previous. */
    uint32_t prev = linux_umask(0022u);
    TEST("default umask is 0022 (Linux convention)");
    if (prev == 0022u) PASS();
    else FAIL("default not 022");
}

static void t_set_returns_previous(void) {
    linux_umask_reset_for_tests();
    (void)linux_umask(0077u);            /* set to 077 */
    uint32_t prev = linux_umask(0027u);  /* read 077, set 027 */
    TEST("umask returns previous mask");
    if (prev == 0077u) PASS();
    else FAIL("previous not surfaced");
}

static void t_high_bits_clamped(void) {
    linux_umask_reset_for_tests();
    (void)linux_umask(0xFFFFFFFFu);
    /* Read it back via another umask call. */
    uint32_t prev = linux_umask(0u);
    TEST("umask clamps to low 9 bits (0777)");
    if (prev == 0777u) PASS();
    else FAIL("high bits not clamped");
}

static void t_zero_mask(void) {
    linux_umask_reset_for_tests();
    (void)linux_umask(0u);
    uint32_t prev = linux_umask(0022u);
    TEST("umask(0) is accepted");
    if (prev == 0u) PASS();
    else FAIL("zero mask not stored");
}

static void t_persistence_across_calls(void) {
    linux_umask_reset_for_tests();
    (void)linux_umask(0077u);
    (void)linux_umask(0017u);
    uint32_t prev = linux_umask(0022u);
    TEST("umask persists across multiple calls");
    if (prev == 0017u) PASS();
    else FAIL("persistence broken");
}

static void t_reset_for_tests_restores_default(void) {
    (void)linux_umask(0044u);
    linux_umask_reset_for_tests();
    uint32_t prev = linux_umask(0022u);
    TEST("reset_for_tests restores default 0022");
    if (prev == 0022u) PASS();
    else FAIL("reset didn't restore default");
}

int test_linux_umask_run(void) {
    printf("[test_linux_umask]\n");
    tests_run = tests_passed = 0;

    t_default_is_022();
    t_set_returns_previous();
    t_high_bits_clamped();
    t_zero_mask();
    t_persistence_across_calls();
    t_reset_for_tests_restores_default();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
