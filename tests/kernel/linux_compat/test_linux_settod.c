#include "kernel/linux_compat/linux_settod.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_calls;
static int64_t g_last_sec;
static int64_t g_last_usec;
static int64_t g_rc;

static int64_t fake_set(int64_t s, int64_t u) {
    g_calls++; g_last_sec = s; g_last_usec = u; return g_rc;
}

static void install_fake(int64_t rc) {
    static struct linux_settod_ops o;
    o.set_seconds = fake_set;
    g_calls = 0; g_last_sec = -1; g_last_usec = -1; g_rc = rc;
    linux_settod_reset_for_tests();
    linux_settod_install_ops(&o);
}

static void t1(void) {
    linux_settod_reset_for_tests();
    TEST("settimeofday(NULL, NULL) -> 0 (no-op)");
    if (linux_settimeofday(NULL, NULL) == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_settod_reset_for_tests();
    struct linux_settod_timeval tv = {1000, 1000000};
    TEST("settimeofday tv_usec >= 1e6 -> -EINVAL");
    if (linux_settimeofday(&tv, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_settod_reset_for_tests();
    struct linux_settod_timeval tv = {1000, -1};
    TEST("settimeofday tv_usec<0 -> -EINVAL");
    if (linux_settimeofday(&tv, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_settod_reset_for_tests();
    struct linux_settod_timeval tv = {-1, 0};
    TEST("settimeofday tv_sec<0 -> -EINVAL");
    if (linux_settimeofday(&tv, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_settod_reset_for_tests();
    struct linux_settod_timeval tv = {1700000000, 500000};
    TEST("settimeofday well-formed -> 0 (no provider)");
    if (linux_settimeofday(&tv, NULL) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake(0);
    struct linux_settod_timeval tv = {42, 99};
    int64_t r = linux_settimeofday(&tv, NULL);
    TEST("settimeofday delegates to provider");
    if (r == 0 && g_calls == 1 && g_last_sec == 42 && g_last_usec == 99)
        PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake(-LINUX_EPERM);
    struct linux_settod_timeval tv = {42, 0};
    TEST("settimeofday provider error forwarded");
    if (linux_settimeofday(&tv, NULL) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_settod_reset_for_tests();
    /* Non-NULL tz should be ignored, not rejected. */
    int dummy_tz = 0;
    TEST("settimeofday(NULL, &tz) -> 0 (tz ignored)");
    if (linux_settimeofday(NULL, &dummy_tz) == 0) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake(-LINUX_EPERM);
    linux_settod_install_ops(NULL);
    struct linux_settod_timeval tv = {42, 0};
    int64_t rc = linux_settimeofday(&tv, NULL);
    TEST("settod install_ops(NULL) clears set_seconds callback");
    if (rc == 0 && g_calls == 0) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake(-LINUX_EPERM);
    linux_settod_reset_for_tests();
    struct linux_settod_timeval tv = {42, 0};
    int64_t rc = linux_settimeofday(&tv, NULL);
    TEST("settod reset clears installed callbacks");
    if (rc == 0 && g_calls == 0) PASS();
    else FAIL("");
}

int test_linux_settod_run(void) {
    printf("[test_linux_settod]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
