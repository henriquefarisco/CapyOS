#include "kernel/linux_compat/linux_sched_prio.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_max(SCHED_FIFO) -> 99");
    if (linux_sched_get_priority_max(LINUX_SCHED_FIFO) == 99) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_max(SCHED_RR) -> 99");
    if (linux_sched_get_priority_max(LINUX_SCHED_RR) == 99) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_max(SCHED_OTHER) -> 0");
    if (linux_sched_get_priority_max(LINUX_SCHED_OTHER) == 0) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_min(SCHED_FIFO) -> 1");
    if (linux_sched_get_priority_min(LINUX_SCHED_FIFO) == 1) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_min(SCHED_OTHER) -> 0");
    if (linux_sched_get_priority_min(LINUX_SCHED_OTHER) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_max(unknown) -> -EINVAL");
    if (linux_sched_get_priority_max(99) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_sched_prio_reset_for_tests();
    TEST("get_priority_min(unknown) -> -EINVAL");
    if (linux_sched_get_priority_min(99) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler unknown policy -> -EINVAL");
    struct linux_sched_param p = { .sched_priority = 0 };
    if (linux_sched_setscheduler(0, 99, &p) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler NULL param -> -EFAULT");
    if (linux_sched_setscheduler(0, LINUX_SCHED_OTHER, NULL) == -LINUX_EFAULT)
        PASS();
    else FAIL("");
}
static void t10(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler(FIFO, prio 0) -> -EINVAL (FIFO needs >=1)");
    struct linux_sched_param p = { .sched_priority = 0 };
    if (linux_sched_setscheduler(0, LINUX_SCHED_FIFO, &p) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t11(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler(FIFO, prio 100) -> -EINVAL (max 99)");
    struct linux_sched_param p = { .sched_priority = 100 };
    if (linux_sched_setscheduler(0, LINUX_SCHED_FIFO, &p) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t12(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler(OTHER, prio 5) -> -EINVAL (OTHER needs 0)");
    struct linux_sched_param p = { .sched_priority = 5 };
    if (linux_sched_setscheduler(0, LINUX_SCHED_OTHER, &p) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t13(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setscheduler(FIFO, 50) -> 0; getscheduler -> SCHED_FIFO");
    struct linux_sched_param p = { .sched_priority = 50 };
    int64_t s = linux_sched_setscheduler(0, LINUX_SCHED_FIFO, &p);
    int64_t g = linux_sched_getscheduler(0);
    if (s == 0 && g == LINUX_SCHED_FIFO) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_sched_prio_reset_for_tests();
    TEST("getscheduler default -> SCHED_OTHER");
    if (linux_sched_getscheduler(0) == LINUX_SCHED_OTHER) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_sched_prio_reset_for_tests();
    TEST("getscheduler pid<0 -> -EINVAL");
    if (linux_sched_getscheduler(-1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_sched_prio_reset_for_tests();
    TEST("getparam NULL -> -EFAULT");
    if (linux_sched_getparam(0, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t17(void) {
    linux_sched_prio_reset_for_tests();
    TEST("getparam fresh -> priority 0");
    struct linux_sched_param p = { .sched_priority = 999 };
    int64_t r = linux_sched_getparam(0, &p);
    if (r == 0 && p.sched_priority == 0) PASS();
    else FAIL("");
}
static void t18(void) {
    linux_sched_prio_reset_for_tests();
    struct linux_sched_param set = { .sched_priority = 50 };
    (void)linux_sched_setscheduler(0, LINUX_SCHED_FIFO, &set);
    struct linux_sched_param p = { .sched_priority = 0 };
    (void)linux_sched_getparam(0, &p);
    TEST("setscheduler/getparam round-trip preserves priority");
    if (p.sched_priority == 50) PASS();
    else FAIL("");
}
static void t19(void) {
    linux_sched_prio_reset_for_tests();
    TEST("setparam NULL -> -EFAULT");
    if (linux_sched_setparam(0, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t20(void) {
    linux_sched_prio_reset_for_tests();
    struct linux_sched_param set = { .sched_priority = 50 };
    (void)linux_sched_setscheduler(0, LINUX_SCHED_FIFO, &set);
    /* Now set a new priority via setparam with current policy. */
    struct linux_sched_param newp = { .sched_priority = 75 };
    int64_t r = linux_sched_setparam(0, &newp);
    struct linux_sched_param got = { .sched_priority = 0 };
    (void)linux_sched_getparam(0, &got);
    TEST("setparam updates priority of current policy");
    if (r == 0 && got.sched_priority == 75) PASS();
    else FAIL("");
}

int test_linux_sched_prio_run(void) {
    printf("[test_linux_sched_prio]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10();
    t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18(); t19(); t20();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
