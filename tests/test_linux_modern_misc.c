#include "kernel/linux_compat/linux_modern_misc.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    int dummy;
    TEST("futex_waitv flags!=0 -> -EINVAL");
    if (linux_futex_waitv(&dummy, 1, 1, NULL, LINUX_FX_CLOCK_MONOTONIC)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    int dummy;
    TEST("futex_waitv nr_futexes=0 -> -EINVAL");
    if (linux_futex_waitv(&dummy, 0, 0, NULL, LINUX_FX_CLOCK_MONOTONIC)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    int dummy;
    TEST("futex_waitv nr_futexes > MAX -> -EINVAL");
    if (linux_futex_waitv(&dummy, LINUX_FUTEX_WAITV_MAX + 1, 0,
                          NULL, LINUX_FX_CLOCK_MONOTONIC)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("futex_waitv NULL waiters -> -EFAULT");
    if (linux_futex_waitv(NULL, 1, 0, NULL, LINUX_FX_CLOCK_MONOTONIC)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t5(void) {
    int dummy;
    TEST("futex_waitv unknown clockid -> -EINVAL");
    if (linux_futex_waitv(&dummy, 1, 0, NULL, 99) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t6(void) {
    int dummy;
    TEST("futex_waitv well-formed -> -ENOSYS (Marco M1 fallback)");
    if (linux_futex_waitv(&dummy, 1, 0, NULL,
                          LINUX_FX_CLOCK_MONOTONIC) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t7(void) {
    int dummy;
    TEST("futex_waitv CLOCK_REALTIME -> -ENOSYS");
    if (linux_futex_waitv(&dummy, 1, 0, NULL,
                          LINUX_FX_CLOCK_REALTIME) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("clock_adjtime clk_id<0 -> -EINVAL");
    struct linux_timex_subset b = {0};
    if (linux_clock_adjtime(-1, &b) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    TEST("clock_adjtime NULL buf -> -EFAULT");
    if (linux_clock_adjtime(0, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t10(void) {
    struct linux_timex_subset b = { .modes = 0 };
    int64_t r = linux_clock_adjtime(0, &b);
    TEST("clock_adjtime read-only -> TIME_OK");
    if (r == LINUX_TIME_OK) PASS();
    else FAIL("");
}
static void t11(void) {
    struct linux_timex_subset b = { .modes = 0xF000 };
    TEST("clock_adjtime unknown mode bits -> -EINVAL");
    if (linux_clock_adjtime(0, &b) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_modern_misc_reset_for_tests();
    int64_t fd = linux_memfd_secret(0);
    TEST("memfd_secret(0) -> first fd in MEMFD_SECRET range");
    if (fd == LINUX_MEMFD_SECRET_FD_BASE) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_modern_misc_reset_for_tests();
    TEST("memfd_secret unknown flags -> -EINVAL");
    if (linux_memfd_secret(0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_modern_misc_reset_for_tests();
    int64_t a = linux_memfd_secret(LINUX_MEMFD_SECRET_CLOEXEC);
    int64_t b = linux_memfd_secret(0);
    TEST("memfd_secret returns distinct fds");
    if (a == LINUX_MEMFD_SECRET_FD_BASE &&
        b == LINUX_MEMFD_SECRET_FD_BASE + 1) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_modern_misc_reset_for_tests();
    int64_t last = -1;
    for (int i = 0; i < LINUX_MEMFD_SECRET_FD_MAX; i++) {
        last = linux_memfd_secret(0);
    }
    int64_t r = linux_memfd_secret(0);
    TEST("memfd_secret table exhausted -> -ENFILE");
    if (last == LINUX_MEMFD_SECRET_FD_BASE +
                LINUX_MEMFD_SECRET_FD_MAX - 1 &&
        r == -LINUX_ENFILE) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_modern_misc_reset_for_tests();
    int64_t fd = linux_memfd_secret(0);
    int64_t c = linux_memfd_secret_close((int)fd);
    int64_t again = linux_memfd_secret(0);
    TEST("memfd_secret close releases slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("");
}
static void t17(void) {
    linux_modern_misc_reset_for_tests();
    int64_t fd = linux_memfd_secret(0);
    char buf[8];
    TEST("memfd_secret read on live fd -> -ENOSYS until backing lands");
    if (linux_memfd_secret_read((int)fd, buf, sizeof(buf)) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t18(void) {
    linux_modern_misc_reset_for_tests();
    TEST("memfd_secret read on unknown fd -> -EBADF");
    if (linux_memfd_secret_read(LINUX_MEMFD_SECRET_FD_BASE, (void *)1, 1)
        == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t19(void) {
    linux_modern_misc_reset_for_tests();
    int64_t fd = linux_memfd_secret(0);
    TEST("memfd_secret write on live fd -> -ENOSYS until backing lands");
    if (linux_memfd_secret_write((int)fd, "x", 1) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t20(void) {
    linux_modern_misc_reset_for_tests();
    int64_t fd = linux_memfd_secret(0);
    TEST("memfd_secret lseek on live fd -> -ENOSYS until backing lands");
    if (linux_memfd_secret_lseek((int)fd, 0, 0) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}

int test_linux_modern_misc_run(void) {
    printf("[test_linux_modern_misc]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14(); t15();
    t16(); t17(); t18(); t19(); t20();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
