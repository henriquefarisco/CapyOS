#include "kernel/linux_compat/linux_time_legacy.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int64_t g_now;
static int g_now_calls;
static int64_t fake_now(void) { g_now_calls++; return g_now; }

static void install_fake(void) {
    static const struct linux_time_legacy_ops o = {
        .now_seconds = fake_now,
    };
    g_now = 0;
    g_now_calls = 0;
    linux_time_legacy_reset_for_tests();
    linux_time_legacy_install_ops(&o);
}

static void t1(void) {
    linux_time_legacy_reset_for_tests();
    TEST("time(NULL) without provider -> 0");
    if (linux_time(NULL) == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_time_legacy_reset_for_tests();
    int64_t out = -1;
    int64_t r = linux_time(&out);
    TEST("time(&out) without provider -> 0/0");
    if (r == 0 && out == 0) PASS();
    else FAIL("");
}
static void t3(void) {
    install_fake();
    g_now = 1700000000;
    int64_t out = 0;
    int64_t r = linux_time(&out);
    TEST("time(&out) with provider returns and writes seconds");
    if (r == 1700000000 && out == 1700000000 && g_now_calls == 1) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake();
    g_now = 42;
    TEST("time(NULL) returns provider seconds");
    if (linux_time(NULL) == 42 && g_now_calls == 1) PASS();
    else FAIL("");
}
static void t5(void) {
    TEST("getcpu(NULL, NULL) -> 0 (no-op)");
    if (linux_getcpu(NULL, NULL) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    uint32_t cpu = 99, node = 99;
    int64_t r = linux_getcpu(&cpu, &node);
    TEST("getcpu writes 0/0 (Marco M1 single-CPU)");
    if (r == 0 && cpu == 0 && node == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    uint32_t cpu = 99;
    int64_t r = linux_getcpu(&cpu, NULL);
    TEST("getcpu(&cpu, NULL) writes only cpu");
    if (r == 0 && cpu == 0) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    g_now = 42;
    linux_time_legacy_install_ops(NULL);
    int64_t out = -1;
    int64_t r = linux_time(&out);
    TEST("time install_ops(NULL) clears now_seconds callback");
    if (r == 0 && out == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    g_now = 42;
    linux_time_legacy_reset_for_tests();
    int64_t out = -1;
    int64_t r = linux_time(&out);
    TEST("time reset clears installed callbacks");
    if (r == 0 && out == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}

int test_linux_time_legacy_run(void) {
    printf("[test_linux_time_legacy]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
