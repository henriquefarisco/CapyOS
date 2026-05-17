#include "kernel/linux_compat/linux_dup.h"
#include "kernel/linux_compat/linux_errno.h"

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

/* --- fake ops --- */

static int g_dup_calls = 0;
static int g_dup2_calls = 0;
static int g_last_old = -1;
static int g_last_new = -1;

static int64_t fake_dup(int oldfd) {
    g_dup_calls++;
    g_last_old = oldfd;
    return oldfd + 100; /* deterministic synthetic new fd */
}
static int64_t fake_dup2(int oldfd, int newfd) {
    g_dup2_calls++;
    g_last_old = oldfd;
    g_last_new = newfd;
    return newfd;
}
static void install_fake(void) {
    static const struct linux_dup_ops ops = {
        .dup = fake_dup,
        .dup2 = fake_dup2,
    };
    g_dup_calls = g_dup2_calls = 0;
    g_last_old = g_last_new = -1;
    linux_dup_reset_for_tests();
    linux_dup_install_ops(&ops);
}

/* --- dup --- */

static void t_dup_negative_ebadf(void) {
    linux_dup_reset_for_tests();
    TEST("dup(-1) -> -EBADF");
    if (linux_dup(-1) == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_dup_no_ops_enosys(void) {
    linux_dup_reset_for_tests();
    TEST("dup(3) without ops -> -ENOSYS");
    if (linux_dup(3) == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_dup_calls_ops(void) {
    install_fake();
    int64_t r = linux_dup(5);
    TEST("dup(5) with ops -> ops.dup invoked, returns 105");
    if (r == 105 && g_dup_calls == 1 && g_last_old == 5) PASS();
    else FAIL("ops.dup not invoked correctly");
}

/* --- dup2 --- */

static void t_dup2_negative_old_ebadf(void) {
    TEST("dup2(-1, 3) -> -EBADF");
    if (linux_dup2(-1, 3) == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced for negative oldfd");
}

static void t_dup2_negative_new_ebadf(void) {
    TEST("dup2(3, -1) -> -EBADF");
    if (linux_dup2(3, -1) == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced for negative newfd");
}

static void t_dup2_same_returns_newfd(void) {
    install_fake();
    int64_t r = linux_dup2(7, 7);
    TEST("dup2(7, 7) -> 7 (no-op, ops.dup2 NOT called)");
    if (r == 7 && g_dup2_calls == 0) PASS();
    else FAIL("same-fd no-op wrong");
}

static void t_dup2_no_ops_enosys(void) {
    linux_dup_reset_for_tests();
    TEST("dup2(3, 4) without ops -> -ENOSYS");
    if (linux_dup2(3, 4) == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_dup2_calls_ops(void) {
    install_fake();
    int64_t r = linux_dup2(3, 9);
    TEST("dup2(3, 9) with ops -> ops.dup2 invoked, returns 9");
    if (r == 9 && g_dup2_calls == 1 &&
        g_last_old == 3 && g_last_new == 9) PASS();
    else FAIL("ops.dup2 not invoked correctly");
}

static void t_dup_install_null_clears(void) {
    install_fake();
    linux_dup_install_ops(NULL);
    TEST("dup_install_ops(NULL) clears -> dup falls back to ENOSYS");
    if (linux_dup(3) == -LINUX_ENOSYS) PASS();
    else FAIL("install(NULL) didn't clear ops");
}

static void t_dup_install_null_clears_dup2(void) {
    install_fake();
    linux_dup_install_ops(NULL);
    TEST("dup_install_ops(NULL) clears -> dup2 falls back to ENOSYS");
    if (linux_dup2(3, 4) == -LINUX_ENOSYS && g_dup2_calls == 0) PASS();
    else FAIL("install(NULL) didn't clear dup2 ops");
}

static void t_dup_reset_clears_callbacks(void) {
    install_fake();
    linux_dup_reset_for_tests();
    int64_t r1 = linux_dup(3);
    int64_t r2 = linux_dup2(3, 4);
    TEST("dup reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_dup_calls == 0 && g_dup2_calls == 0) PASS();
    else FAIL("reset didn't clear callbacks");
}

int test_linux_dup_run(void) {
    printf("[test_linux_dup]\n");
    tests_run = tests_passed = 0;

    t_dup_negative_ebadf();
    t_dup_no_ops_enosys();
    t_dup_calls_ops();

    t_dup2_negative_old_ebadf();
    t_dup2_negative_new_ebadf();
    t_dup2_same_returns_newfd();
    t_dup2_no_ops_enosys();
    t_dup2_calls_ops();

    t_dup_install_null_clears();
    t_dup_install_null_clears_dup2();
    t_dup_reset_clears_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
