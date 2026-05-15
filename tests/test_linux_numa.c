#include "kernel/linux_compat/linux_numa.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    int p = -1;
    int64_t r = linux_get_mempolicy(&p, NULL, 0, NULL, 0);
    TEST("get_mempolicy default -> MPOL_DEFAULT");
    if (r == 0 && p == LINUX_MPOL_DEFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    uint64_t mask[2] = {0xFFFF, 0xFFFF};
    int64_t r = linux_get_mempolicy(NULL, mask, 64, NULL, 0);
    TEST("get_mempolicy nodemask -> bit 0 set, rest cleared");
    if (r == 0 && mask[0] == 1 && mask[1] == 0xFFFF /* untouched */) PASS();
    else FAIL("");
}
static void t3(void) {
    uint64_t mask[1];
    TEST("get_mempolicy unknown flags -> -EINVAL");
    if (linux_get_mempolicy(NULL, mask, 64, NULL, 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    uint64_t mask[1];
    TEST("get_mempolicy nodemask non-NULL with maxnode=0 -> -EINVAL");
    if (linux_get_mempolicy(NULL, mask, 0, NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    int64_t r = linux_get_mempolicy(NULL, NULL, 0, NULL, 0);
    TEST("get_mempolicy NULL/NULL -> 0 (no-op probe)");
    if (r == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    TEST("set_mempolicy unknown policy -> -EINVAL");
    if (linux_set_mempolicy(99, NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t7(void) {
    int64_t r = linux_set_mempolicy(LINUX_MPOL_DEFAULT, NULL, 0);
    TEST("set_mempolicy(DEFAULT, NULL, 0) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("set_mempolicy(BIND, NULL, 0) -> -EINVAL (need mask)");
    if (linux_set_mempolicy(LINUX_MPOL_BIND, NULL, 0) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t9(void) {
    uint64_t mask = 1;
    int64_t r = linux_set_mempolicy(LINUX_MPOL_BIND, &mask, 64);
    TEST("set_mempolicy(BIND, mask, 64) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t10(void) {
    uint64_t mask = 1;
    int64_t r = linux_set_mempolicy(LINUX_MPOL_INTERLEAVE, &mask, 64);
    TEST("set_mempolicy(INTERLEAVE, mask, 64) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("mbind unknown policy -> -EINVAL");
    if (linux_mbind(NULL, 4096, 99, NULL, 0, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("mbind unknown flags -> -EINVAL");
    if (linux_mbind(NULL, 4096, LINUX_MPOL_DEFAULT, NULL, 0, 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("mbind(BIND, NULL, 0, ...) -> -EINVAL (need mask)");
    if (linux_mbind(NULL, 4096, LINUX_MPOL_BIND, NULL, 0, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    uint64_t mask = 1;
    int64_t r = linux_mbind(NULL, 4096, LINUX_MPOL_BIND, &mask, 64,
                            LINUX_MPOL_MF_STRICT);
    TEST("mbind(BIND, mask, STRICT) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t15(void) {
    int64_t r = linux_mbind(NULL, 4096, LINUX_MPOL_DEFAULT, NULL, 0,
                            LINUX_MPOL_MF_MOVE | LINUX_MPOL_MF_MOVE_ALL);
    TEST("mbind(DEFAULT, NULL, MOVE|MOVE_ALL) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}

int test_linux_numa_run(void) {
    printf("[test_linux_numa]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
