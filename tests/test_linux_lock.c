#include "kernel/linux_compat/linux_lock.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_copy_calls;
static int g_last_in;
static int g_last_out;
static size_t g_last_len;

static int64_t fake_copy(int fd_in, int64_t *off_in,
                         int fd_out, int64_t *off_out,
                         size_t len, uint32_t flags) {
    g_copy_calls++;
    g_last_in = fd_in; g_last_out = fd_out; g_last_len = len;
    (void)off_in; (void)off_out; (void)flags;
    return (int64_t)len;
}

static void install_fake(void) {
    static const struct linux_lock_ops o = {
        .copy_file_range = fake_copy,
    };
    g_copy_calls = 0;
    g_last_in = g_last_out = -1;
    g_last_len = 0;
    linux_lock_reset_for_tests();
    linux_lock_install_ops(&o);
}

static void t1(void) {
    linux_lock_reset_for_tests();
    TEST("flock(-1, ...) -> -EBADF");
    if (linux_flock(-1, LINUX_LOCK_EX) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_lock_reset_for_tests();
    TEST("flock unknown bit -> -EINVAL");
    if (linux_flock(7, 0x80) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_lock_reset_for_tests();
    TEST("flock(LOCK_SH | LOCK_EX) -> -EINVAL (mutually exclusive)");
    if (linux_flock(7, LINUX_LOCK_SH | LINUX_LOCK_EX) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t4(void) {
    linux_lock_reset_for_tests();
    TEST("flock(LOCK_EX) on fresh fd -> 0");
    if (linux_flock(7, LINUX_LOCK_EX) == 0) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_lock_reset_for_tests();
    TEST("flock(LOCK_SH) on fresh fd -> 0");
    if (linux_flock(7, LINUX_LOCK_SH) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_lock_reset_for_tests();
    (void)linux_flock(7, LINUX_LOCK_EX);
    TEST("flock(LOCK_EX | LOCK_NB) re-lock -> 0 (single proc)");
    if (linux_flock(7, LINUX_LOCK_EX | LINUX_LOCK_NB) == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_lock_reset_for_tests();
    (void)linux_flock(7, LINUX_LOCK_EX);
    TEST("flock(LOCK_UN) on locked fd -> 0");
    if (linux_flock(7, LINUX_LOCK_UN) == 0) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_lock_reset_for_tests();
    TEST("flock(LOCK_UN) on unlocked fd -> 0 (no-op)");
    if (linux_flock(7, LINUX_LOCK_UN) == 0) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_lock_reset_for_tests();
    /* Fill all 32 slots, then 33rd -> -ENOLCK. */
    for (int i = 0; i < 32; i++) {
        if (linux_flock(100 + i, LINUX_LOCK_EX) != 0) {
            TEST("flock 32-slot fill setup");
            FAIL("setup-failed");
            return;
        }
    }
    TEST("flock 33rd distinct fd -> -ENOLCK");
    if (linux_flock(200, LINUX_LOCK_EX) == -LINUX_ENOLCK) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_lock_reset_for_tests();
    TEST("flock(LOCK_UN | LOCK_NB) -> 0 (NB ignored on unlock)");
    if (linux_flock(7, LINUX_LOCK_UN | LINUX_LOCK_NB) == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_lock_reset_for_tests();
    TEST("copy_file_range(-1, ..., out, ...) -> -EBADF");
    if (linux_copy_file_range(-1, NULL, 7, NULL, 100, 0) == -LINUX_EBADF)
        PASS();
    else FAIL("");
}
static void t12(void) {
    linux_lock_reset_for_tests();
    TEST("copy_file_range(in, ..., -1, ...) -> -EBADF");
    if (linux_copy_file_range(7, NULL, -1, NULL, 100, 0) == -LINUX_EBADF)
        PASS();
    else FAIL("");
}
static void t13(void) {
    linux_lock_reset_for_tests();
    TEST("copy_file_range non-zero flags -> -EINVAL");
    if (linux_copy_file_range(7, NULL, 8, NULL, 100, 1) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t14(void) {
    linux_lock_reset_for_tests();
    TEST("copy_file_range without ops -> -ENOSYS");
    if (linux_copy_file_range(7, NULL, 8, NULL, 100, 0) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    int64_t rc = linux_copy_file_range(7, NULL, 8, NULL, 100, 0);
    TEST("copy_file_range delegates to provider");
    if (rc == 100 && g_copy_calls == 1 &&
        g_last_in == 7 && g_last_out == 8 && g_last_len == 100) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    linux_lock_install_ops(NULL);
    int64_t rc = linux_copy_file_range(7, NULL, 8, NULL, 100, 0);
    TEST("lock install_ops(NULL) clears copy_file_range callback");
    if (rc == -LINUX_ENOSYS && g_copy_calls == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    linux_lock_reset_for_tests();
    int64_t rc = linux_copy_file_range(7, NULL, 8, NULL, 100, 0);
    TEST("lock reset clears installed callbacks");
    if (rc == -LINUX_ENOSYS && g_copy_calls == 0) PASS();
    else FAIL("");
}

int test_linux_lock_run(void) {
    printf("[test_linux_lock]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15();
    t16(); t17();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
