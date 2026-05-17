#include "kernel/linux_compat/linux_brk.h"

#include <stdint.h>
#include <stddef.h>
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

/* Fake reserve_pages: counts calls + tracks last range; can be
 * primed to fail on demand. */
static struct {
    int calls;
    uint64_t last_start;
    size_t   last_pages;
    int      next_rc;
} g_fake;

static int fake_reserve(uint64_t start_va, size_t pages) {
    g_fake.calls++;
    g_fake.last_start = start_va;
    g_fake.last_pages = pages;
    return g_fake.next_rc;
}

static void install_fake(void) {
    linux_brk_reset_for_tests();
    g_fake.calls = 0;
    g_fake.last_start = 0;
    g_fake.last_pages = 0;
    g_fake.next_rc = 0;
    static const struct linux_brk_ops ops = { .reserve_pages = fake_reserve };
    linux_brk_install_ops(&ops);
}

/* ---- Tests ---- */

static void t_query_returns_base_initially(void) {
    install_fake();
    int64_t r = linux_brk(0);
    TEST("brk(0) initial -> LINUX_BRK_BASE");
    if ((uint64_t)r == LINUX_BRK_BASE && g_fake.calls == 0) PASS();
    else FAIL("base mismatch or unexpected reserve call");
}

static void t_grow_within_one_page_reserves_one_page(void) {
    install_fake();
    int64_t r = linux_brk(LINUX_BRK_BASE + 100);
    TEST("brk(base+100) reserves 1 page (round up to 4 KiB)");
    if ((uint64_t)r == LINUX_BRK_BASE + 100 &&
        g_fake.calls == 1 &&
        g_fake.last_start == LINUX_BRK_BASE &&
        g_fake.last_pages == 1) PASS();
    else FAIL("grow path wrong");
}

static void t_grow_to_exact_page_does_not_extra_reserve(void) {
    install_fake();
    /* First grow to 4 KiB. */
    (void)linux_brk(LINUX_BRK_BASE + 4096);
    int prev_calls = g_fake.calls;
    /* Grow by 1 byte more -> need second page. */
    int64_t r2 = linux_brk(LINUX_BRK_BASE + 4097);
    TEST("brk: crossing page boundary triggers exactly one new reserve");
    if ((uint64_t)r2 == LINUX_BRK_BASE + 4097 &&
        g_fake.calls == prev_calls + 1 &&
        g_fake.last_pages == 1) PASS();
    else FAIL("page boundary handling wrong");
}

static void t_shrink_does_not_release(void) {
    install_fake();
    (void)linux_brk(LINUX_BRK_BASE + 8192);
    int calls_after_grow = g_fake.calls;
    int64_t r = linux_brk(LINUX_BRK_BASE + 100);
    TEST("brk shrink: returns new break, no reserve calls");
    if ((uint64_t)r == LINUX_BRK_BASE + 100 &&
        g_fake.calls == calls_after_grow) PASS();
    else FAIL("shrink path called reserve or wrong return");
}

static void t_below_base_returns_current(void) {
    install_fake();
    (void)linux_brk(LINUX_BRK_BASE + 1000);
    int64_t r = linux_brk(LINUX_BRK_BASE - 1);
    TEST("brk: addr < base -> current break unchanged");
    if ((uint64_t)r == LINUX_BRK_BASE + 1000) PASS();
    else FAIL("low-addr path wrong");
}

static void t_above_max_returns_current(void) {
    install_fake();
    (void)linux_brk(LINUX_BRK_BASE + 1000);
    int64_t r = linux_brk(LINUX_BRK_BASE + LINUX_BRK_MAX_SIZE + 1);
    TEST("brk: addr > base+MAX -> current break unchanged");
    if ((uint64_t)r == LINUX_BRK_BASE + 1000) PASS();
    else FAIL("over-max path wrong");
}

static void t_reserve_failure_keeps_old_break(void) {
    install_fake();
    g_fake.next_rc = -1;
    int64_t r = linux_brk(LINUX_BRK_BASE + 5000);
    TEST("brk: reserve_pages failure -> current break unchanged");
    if ((uint64_t)r == LINUX_BRK_BASE) PASS();
    else FAIL("failure mode wrong");
}

static void t_no_ops_installed_returns_current(void) {
    linux_brk_reset_for_tests();
    /* No install_fake. */
    int64_t r = linux_brk(LINUX_BRK_BASE + 5000);
    TEST("brk: no reserve callback -> current break unchanged");
    if ((uint64_t)r == LINUX_BRK_BASE) PASS();
    else FAIL("missing-ops mode wrong");
}

static void t_query_after_grow_reflects_new_break(void) {
    install_fake();
    (void)linux_brk(LINUX_BRK_BASE + 1234);
    int64_t r = linux_brk(0);
    TEST("brk(0) after grow returns the live break");
    if ((uint64_t)r == LINUX_BRK_BASE + 1234) PASS();
    else FAIL("query after grow wrong");
}

static void t_no_re_reserve_when_within_committed(void) {
    install_fake();
    /* Grow to span 2 pages (8 KiB). */
    (void)linux_brk(LINUX_BRK_BASE + 8192);
    int calls_after_grow = g_fake.calls;
    /* Shrink and grow within the already-committed window. */
    (void)linux_brk(LINUX_BRK_BASE + 100);
    int64_t r = linux_brk(LINUX_BRK_BASE + 4000);
    TEST("brk: re-grow within already-committed window: no extra reserve");
    if ((uint64_t)r == LINUX_BRK_BASE + 4000 &&
        g_fake.calls == calls_after_grow) PASS();
    else FAIL("redundant reserve issued");
}

static void t_current_accessor_matches(void) {
    install_fake();
    (void)linux_brk(LINUX_BRK_BASE + 999);
    TEST("linux_brk_current() reflects break after a successful grow");
    if (linux_brk_current() == LINUX_BRK_BASE + 999) PASS();
    else FAIL("accessor mismatch");
}

static void t_install_null_clears_reserve_callback(void) {
    install_fake();
    linux_brk_install_ops(NULL);
    int64_t r = linux_brk(LINUX_BRK_BASE + 5000);
    TEST("brk install_ops(NULL) clears reserve_pages callback");
    if ((uint64_t)r == LINUX_BRK_BASE && g_fake.calls == 0) PASS();
    else FAIL("stale reserve callback invoked");
}

static void t_reset_clears_reserve_callback(void) {
    install_fake();
    linux_brk_reset_for_tests();
    int64_t r = linux_brk(LINUX_BRK_BASE + 5000);
    TEST("brk reset clears installed callbacks");
    if ((uint64_t)r == LINUX_BRK_BASE && g_fake.calls == 0) PASS();
    else FAIL("stale reserve callback invoked");
}

int test_linux_brk_run(void) {
    printf("[test_linux_brk]\n");
    tests_run = tests_passed = 0;

    t_query_returns_base_initially();
    t_grow_within_one_page_reserves_one_page();
    t_grow_to_exact_page_does_not_extra_reserve();
    t_shrink_does_not_release();
    t_below_base_returns_current();
    t_above_max_returns_current();
    t_reserve_failure_keeps_old_break();
    t_no_ops_installed_returns_current();
    t_query_after_grow_reflects_new_break();
    t_no_re_reserve_when_within_committed();
    t_current_accessor_matches();
    t_install_null_clears_reserve_callback();
    t_reset_clears_reserve_callback();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
