#include "kernel/linux_compat/linux_rlimit_legacy.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int provider_get_calls;
static int provider_set_calls;
static int last_resource;
static int64_t provider_get(int r, struct linux_rlimit *out) {
    provider_get_calls++; last_resource = r;
    out->rlim_cur = 42; out->rlim_max = 1024;
    return 0;
}
static int64_t provider_set(int r, const struct linux_rlimit *in) {
    provider_set_calls++; last_resource = r; (void)in;
    return 0;
}

static void install_fake(void) {
    static const struct linux_rlimit_legacy_ops o = {
        .get_limit = provider_get,
        .set_limit = provider_set,
    };
    provider_get_calls = provider_set_calls = 0;
    last_resource = -1;
    linux_rlimit_legacy_reset_for_tests();
    linux_rlimit_legacy_install_ops(&o);
}

static void t1(void) {
    linux_rlimit_legacy_reset_for_tests();
    TEST("getrlimit invalid resource -> -EINVAL");
    struct linux_rlimit r;
    if (linux_getrlimit(-1, &r) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_rlimit_legacy_reset_for_tests();
    TEST("getrlimit out-of-range resource -> -EINVAL");
    struct linux_rlimit r;
    if (linux_getrlimit(99, &r) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_rlimit_legacy_reset_for_tests();
    TEST("getrlimit NULL buf -> -EFAULT");
    if (linux_getrlimit(LINUX_RLIMIT_NOFILE, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_NOFILE, &r);
    TEST("getrlimit NOFILE default -> 1024/4096");
    if (rc == 0 && r.rlim_cur == 1024 && r.rlim_max == 4096) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_STACK, &r);
    TEST("getrlimit STACK default -> 8 MiB / INFINITY");
    if (rc == 0 && r.rlim_cur == 8 * 1024 * 1024 &&
        r.rlim_max == LINUX_RLIM_INFINITY) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_CORE, &r);
    TEST("getrlimit CORE default -> 0 / INFINITY");
    if (rc == 0 && r.rlim_cur == 0 &&
        r.rlim_max == LINUX_RLIM_INFINITY) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_AS, &r);
    TEST("getrlimit AS default -> INFINITY/INFINITY (unlimited)");
    if (rc == 0 && r.rlim_cur == LINUX_RLIM_INFINITY &&
        r.rlim_max == LINUX_RLIM_INFINITY) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_NPROC, &r);
    TEST("getrlimit delegates to provider when installed");
    if (rc == 0 && provider_get_calls == 1 &&
        last_resource == LINUX_RLIMIT_NPROC &&
        r.rlim_cur == 42 && r.rlim_max == 1024) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_rlimit_legacy_reset_for_tests();
    TEST("setrlimit invalid resource -> -EINVAL");
    struct linux_rlimit r = {0, 0};
    if (linux_setrlimit(-1, &r) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_rlimit_legacy_reset_for_tests();
    TEST("setrlimit NULL buf -> -EFAULT");
    if (linux_setrlimit(LINUX_RLIMIT_NOFILE, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r = { .rlim_cur = 100, .rlim_max = 50 };
    TEST("setrlimit cur > max -> -EINVAL");
    if (linux_setrlimit(LINUX_RLIMIT_NOFILE, &r) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r = { .rlim_cur = 50, .rlim_max = LINUX_RLIM_INFINITY };
    TEST("setrlimit cur < INFINITY ok");
    if (linux_setrlimit(LINUX_RLIMIT_NOFILE, &r) == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r = { .rlim_cur = LINUX_RLIM_INFINITY,
                              .rlim_max = LINUX_RLIM_INFINITY };
    TEST("setrlimit both INFINITY ok (unlimited)");
    if (linux_setrlimit(LINUX_RLIMIT_NOFILE, &r) == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    struct linux_rlimit r = { .rlim_cur = 100, .rlim_max = 200 };
    int64_t rc = linux_setrlimit(LINUX_RLIMIT_STACK, &r);
    TEST("setrlimit delegates to provider");
    if (rc == 0 && provider_set_calls == 1 &&
        last_resource == LINUX_RLIMIT_STACK) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    linux_rlimit_legacy_install_ops(NULL);
    struct linux_rlimit r;
    int64_t rc = linux_getrlimit(LINUX_RLIMIT_NPROC, &r);
    TEST("rlimit install_ops(NULL) clears get_limit callback");
    if (rc == 0 && provider_get_calls == 0 &&
        r.rlim_cur == 1024 && r.rlim_max == 1024) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    linux_rlimit_legacy_install_ops(NULL);
    struct linux_rlimit r = { .rlim_cur = 100, .rlim_max = 200 };
    int64_t rc = linux_setrlimit(LINUX_RLIMIT_STACK, &r);
    TEST("rlimit install_ops(NULL) clears set_limit callback");
    if (rc == 0 && provider_set_calls == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    linux_rlimit_legacy_reset_for_tests();
    struct linux_rlimit r = { .rlim_cur = 50, .rlim_max = 100 };
    int64_t r1 = linux_getrlimit(LINUX_RLIMIT_NOFILE, &r);
    int64_t r2 = linux_setrlimit(LINUX_RLIMIT_NOFILE, &r);
    TEST("rlimit reset clears installed callbacks");
    if (r1 == 0 && r2 == 0 &&
        provider_get_calls == 0 && provider_set_calls == 0 &&
        r.rlim_cur == 1024 && r.rlim_max == 4096) PASS();
    else FAIL("");
}

int test_linux_rlimit_legacy_run(void) {
    printf("[test_linux_rlimit_legacy]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14();
    t15(); t16(); t17();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
