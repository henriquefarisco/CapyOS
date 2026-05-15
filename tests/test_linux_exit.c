#include "kernel/linux_compat/linux_exit.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* Test stub: records the exit code, then longjmps back to the
 * test driver so the test runner can keep going. Production
 * task_exit is noreturn; we emulate that without actually
 * killing the process. */
static int     g_recorded_code;
static int     g_calls;
static jmp_buf g_jmp;

static void fake_exit(int code) {
    g_recorded_code = code;
    g_calls++;
    longjmp(g_jmp, 1);
}

static void install_fake(void) {
    linux_exit_reset_for_tests();
    g_recorded_code = -1;
    g_calls = 0;
    static const struct linux_exit_ops ops = { .exit_task = fake_exit };
    linux_exit_install_ops(&ops);
}

/* ---- Tests ---- */

static void t_exit_invokes_callback_with_code(void) {
    install_fake();
    if (setjmp(g_jmp) == 0) {
        (void)linux_exit(42);
        TEST("linux_exit: should not return when ops installed");
        FAIL("returned without longjmp");
        return;
    }
    TEST("linux_exit(42) calls exit_task(42)");
    if (g_recorded_code == 42 && g_calls == 1) PASS();
    else FAIL("code or call count wrong");
}

static void t_exit_group_invokes_callback_with_code(void) {
    install_fake();
    if (setjmp(g_jmp) == 0) {
        (void)linux_exit_group(7);
        TEST("linux_exit_group: should not return when ops installed");
        FAIL("returned without longjmp");
        return;
    }
    TEST("linux_exit_group(7) calls exit_task(7)");
    if (g_recorded_code == 7 && g_calls == 1) PASS();
    else FAIL("code or call count wrong");
}

static void t_exit_zero_is_valid(void) {
    install_fake();
    if (setjmp(g_jmp) == 0) {
        (void)linux_exit(0);
        TEST("linux_exit(0): callback fired");
        FAIL("returned without longjmp");
        return;
    }
    TEST("linux_exit(0) accepted (success status)");
    if (g_recorded_code == 0 && g_calls == 1) PASS();
    else FAIL("zero code mishandled");
}

static void t_exit_negative_passed_through(void) {
    install_fake();
    if (setjmp(g_jmp) == 0) {
        (void)linux_exit(-1);
        TEST("linux_exit(-1): callback fired");
        FAIL("returned without longjmp");
        return;
    }
    TEST("linux_exit(-1): negative code passed through to callback");
    if (g_recorded_code == -1 && g_calls == 1) PASS();
    else FAIL("negative code mishandled");
}

static void t_no_ops_returns_enosys_for_exit(void) {
    linux_exit_reset_for_tests();
    int64_t r = linux_exit(0);
    TEST("linux_exit without ops -> -ENOSYS sentinel");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_no_ops_returns_enosys_for_exit_group(void) {
    linux_exit_reset_for_tests();
    int64_t r = linux_exit_group(0);
    TEST("linux_exit_group without ops -> -ENOSYS sentinel");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_install_null_clears_exit_callback(void) {
    install_fake();
    linux_exit_install_ops(NULL);
    int64_t r = linux_exit(12);
    TEST("linux_exit install_ops(NULL) clears exit_task callback");
    if (r == -LINUX_ENOSYS && g_calls == 0) PASS();
    else FAIL("stale exit callback invoked");
}

static void t_reset_clears_exit_callback(void) {
    install_fake();
    linux_exit_reset_for_tests();
    int64_t r = linux_exit_group(12);
    TEST("linux_exit reset clears installed callbacks");
    if (r == -LINUX_ENOSYS && g_calls == 0) PASS();
    else FAIL("stale exit callback invoked");
}

int test_linux_exit_run(void) {
    printf("[test_linux_exit]\n");
    tests_run = tests_passed = 0;

    t_exit_invokes_callback_with_code();
    t_exit_group_invokes_callback_with_code();
    t_exit_zero_is_valid();
    t_exit_negative_passed_through();
    t_no_ops_returns_enosys_for_exit();
    t_no_ops_returns_enosys_for_exit_group();
    t_install_null_clears_exit_callback();
    t_reset_clears_exit_callback();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
