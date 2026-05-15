#include "kernel/linux_compat/linux_pkey.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_alloc flags!=0 -> -EINVAL");
    if (linux_pkey_alloc(1, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_alloc unknown access bits -> -EINVAL");
    if (linux_pkey_alloc(0, 0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_pkey_reset_for_tests();
    int64_t k = linux_pkey_alloc(0, 0);
    TEST("pkey_alloc(0, 0) -> first user key (>=2)");
    if (k >= LINUX_PKEY_MAX || k < 2) FAIL("");
    else PASS();
}
static void t4(void) {
    linux_pkey_reset_for_tests();
    int64_t k1 = linux_pkey_alloc(0, 0);
    int64_t k2 = linux_pkey_alloc(0, LINUX_PKEY_DISABLE_WRITE);
    TEST("pkey_alloc returns distinct keys");
    if (k1 != k2 && k1 >= 2 && k2 >= 2) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_pkey_reset_for_tests();
    /* Allocate all 14 user-available keys (0/1 reserved). */
    int allocated = 0;
    for (int i = 2; i < LINUX_PKEY_MAX; i++) {
        if (linux_pkey_alloc(0, 0) >= 2) allocated++;
    }
    int64_t r = linux_pkey_alloc(0, 0);
    TEST("pkey_alloc exhaustion -> -ENOSPC");
    if (allocated == 14 && r == -LINUX_ENOSPC) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_pkey_reset_for_tests();
    int64_t k = linux_pkey_alloc(0, 0);
    int64_t r = linux_pkey_free((int)k);
    TEST("pkey_free of allocated key -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_free reserved key 0 -> -EINVAL");
    if (linux_pkey_free(0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_free reserved key 1 -> -EINVAL");
    if (linux_pkey_free(1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_free unallocated key -> -EINVAL");
    if (linux_pkey_free(5) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_pkey_reset_for_tests();
    int64_t k = linux_pkey_alloc(0, 0);
    (void)linux_pkey_free((int)k);
    int64_t k2 = linux_pkey_alloc(0, 0);
    TEST("pkey_alloc reuses freed key");
    if (k2 == k) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_mprotect addr not page-aligned -> -EINVAL");
    if (linux_pkey_mprotect(0x123, 4096,
                            LINUX_PROT_READ, -1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_mprotect len=0 -> 0 (no-op)");
    if (linux_pkey_mprotect(0x1000, 0,
                            LINUX_PROT_READ, -1) == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_mprotect unknown prot bit -> -EINVAL");
    if (linux_pkey_mprotect(0x1000, 4096, 0x80, -1)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_mprotect pkey out of range -> -EINVAL");
    if (linux_pkey_mprotect(0x1000, 4096, LINUX_PROT_READ, 99)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_pkey_reset_for_tests();
    TEST("pkey_mprotect unallocated pkey -> -EINVAL");
    if (linux_pkey_mprotect(0x1000, 4096, LINUX_PROT_READ, 5)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_pkey_reset_for_tests();
    int64_t k = linux_pkey_alloc(0, 0);
    int64_t r = linux_pkey_mprotect(0x1000, 4096,
                                    LINUX_PROT_READ | LINUX_PROT_WRITE,
                                    (int)k);
    TEST("pkey_mprotect allocated pkey -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    linux_pkey_reset_for_tests();
    int64_t r = linux_pkey_mprotect(0x1000, 4096, LINUX_PROT_READ, -1);
    TEST("pkey_mprotect pkey=-1 (default key) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}

int test_linux_pkey_run(void) {
    printf("[test_linux_pkey]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
