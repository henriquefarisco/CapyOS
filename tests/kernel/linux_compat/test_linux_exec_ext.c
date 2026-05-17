#include "kernel/linux_compat/linux_exec_ext.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_close_calls;
static int g_cloexec_calls;
static int g_close_first;
static int g_close_last;

static int64_t fake_close(int fd) {
    g_close_calls++;
    if (g_close_first == -1) g_close_first = fd;
    g_close_last = fd;
    return 0;
}
static int64_t fake_cloexec(int fd) {
    g_cloexec_calls++;
    g_close_last = fd;
    return 0;
}

static void install_fake(void) {
    static struct linux_exec_ext_ops o;
    o.close_one = fake_close;
    o.set_cloexec_one = fake_cloexec;
    g_close_calls = 0;
    g_cloexec_calls = 0;
    g_close_first = -1;
    g_close_last = -1;
    linux_exec_ext_reset_for_tests();
    linux_exec_ext_install_ops(&o);
}

static void t1(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat unknown flags -> -EINVAL");
    if (linux_execveat(LINUX_AT_FDCWD, "/x", NULL, NULL, 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat dirfd<0 (not AT_FDCWD) -> -EBADF");
    if (linux_execveat(-50, "/x", NULL, NULL, 0) == -LINUX_EBADF)
        PASS();
    else FAIL("");
}
static void t3(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat NULL pathname without AT_EMPTY_PATH -> -EFAULT");
    if (linux_execveat(LINUX_AT_FDCWD, NULL, NULL, NULL, 0)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat empty pathname without AT_EMPTY_PATH -> -ENOENT");
    if (linux_execveat(LINUX_AT_FDCWD, "", NULL, NULL, 0)
        == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat well-formed -> -ENOSYS (exec not landed)");
    if (linux_execveat(LINUX_AT_FDCWD, "/bin/sh", NULL, NULL, 0)
        == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_exec_ext_reset_for_tests();
    TEST("execveat empty path with AT_EMPTY_PATH valid -> -ENOSYS");
    if (linux_execveat(7, "", NULL, NULL, LINUX_AT_EMPTY_PATH)
        == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_exec_ext_reset_for_tests();
    TEST("close_range first>last -> -EINVAL");
    if (linux_close_range(10, 5, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_exec_ext_reset_for_tests();
    TEST("close_range unknown flags -> -EINVAL");
    if (linux_close_range(0, 100, 0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_exec_ext_reset_for_tests();
    TEST("close_range without ops -> 0 (validation only)");
    if (linux_close_range(0, 100, 0) == 0) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    int64_t r = linux_close_range(3, 5, 0);
    TEST("close_range delegates to close callback");
    if (r == 0 && g_close_calls == 3 && g_close_first == 3 &&
        g_close_last == 5 && g_cloexec_calls == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    int64_t r = linux_close_range(3, 4, LINUX_CLOSE_RANGE_CLOEXEC);
    TEST("close_range CLOEXEC delegates to set_cloexec callback");
    if (r == 0 && g_cloexec_calls == 2 && g_close_calls == 0) PASS();
    else FAIL("");
}
static void t12(void) {
    install_fake();
    /* Special last == ~0u should be capped at 4096 to avoid
     * huge loop. */
    int64_t r = linux_close_range(0, ~0u, 0);
    TEST("close_range last=~0u capped at 4096 (no infinite loop)");
    if (r == 0 && g_close_calls == 4097) PASS();
    else FAIL("");
}
static void t13(void) {
    install_fake();
    linux_exec_ext_install_ops(NULL);
    int64_t r = linux_close_range(3, 5, 0);
    TEST("exec_ext install_ops(NULL) clears close_range close callback");
    if (r == 0 && g_close_calls == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    linux_exec_ext_install_ops(NULL);
    int64_t r = linux_close_range(3, 4, LINUX_CLOSE_RANGE_CLOEXEC);
    TEST("exec_ext install_ops(NULL) clears close_range CLOEXEC callback");
    if (r == 0 && g_cloexec_calls == 0) PASS();
    else FAIL("");
}

int test_linux_exec_ext_run(void) {
    printf("[test_linux_exec_ext]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12();
    t13(); t14();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
