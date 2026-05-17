#include "kernel/linux_compat/linux_posix_timer.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_posix_timer_reset_for_tests();
    TEST("timer_create NULL timerid -> -EFAULT");
    if (linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, NULL)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_posix_timer_reset_for_tests();
    int id = -1;
    TEST("timer_create unknown clockid -> -EINVAL");
    if (linux_timer_create(99, NULL, &id) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_posix_timer_reset_for_tests();
    int id = -1;
    int64_t r = linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    TEST("timer_create(NULL sevp) -> ok with id 1");
    if (r == 0 && id == 1) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_posix_timer_reset_for_tests();
    struct linux_sigevent_subset sev = {
        .sigev_notify = LINUX_SIGEV_THREAD,
        .sigev_signo = 0,
    };
    int id = -1;
    int64_t r = linux_timer_create(LINUX_CLOCK_REALTIME, &sev, &id);
    TEST("timer_create(SIGEV_THREAD) -> ok");
    if (r == 0 && id == 1) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_posix_timer_reset_for_tests();
    struct linux_sigevent_subset sev = {
        .sigev_notify = 99,
        .sigev_signo = 0,
    };
    int id = -1;
    TEST("timer_create unknown sigev_notify -> -EINVAL");
    if (linux_timer_create(LINUX_CLOCK_MONOTONIC, &sev, &id)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_posix_timer_reset_for_tests();
    /* Fill all 16 slots, then 17th -> -EAGAIN. */
    int id;
    int ok = 1;
    for (int i = 0; i < LINUX_POSIX_TIMER_MAX; i++) {
        if (linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id) != 0) {
            ok = 0; break;
        }
    }
    int64_t r = linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    TEST("timer_create 17th -> -EAGAIN (table exhausted)");
    if (ok && r == -LINUX_EAGAIN) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_posix_timer_reset_for_tests();
    TEST("timer_settime invalid id -> -EINVAL");
    struct linux_itimerspec sp = {{0,0},{1,0}};
    if (linux_timer_settime(99, 0, &sp, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    TEST("timer_settime NULL new -> -EFAULT");
    if (linux_timer_settime(id, 0, NULL, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec sp = {{0,0},{1,1000000000}};
    TEST("timer_settime tv_nsec >=1e9 -> -EINVAL");
    if (linux_timer_settime(id, 0, &sp, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec sp = {{0,0},{-1,0}};
    TEST("timer_settime tv_sec<0 -> -EINVAL");
    if (linux_timer_settime(id, 0, &sp, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec sp = {{1,500},{2,1000}};
    TEST("timer_settime unknown flags -> -EINVAL");
    if (linux_timer_settime(id, 0xDEAD, &sp, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec sp = {{1,500},{2,1000}};
    int64_t r = linux_timer_settime(id, LINUX_TIMER_ABSTIME, &sp, NULL);
    TEST("timer_settime ABSTIME ok");
    if (r == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec set = {{1,500},{2,1000}};
    (void)linux_timer_settime(id, 0, &set, NULL);
    struct linux_itimerspec got;
    int64_t r = linux_timer_gettime(id, &got);
    TEST("timer_settime + timer_gettime round-trip");
    if (r == 0 && got.it_interval.tv_sec == 1 &&
        got.it_value.tv_sec == 2 && got.it_value.tv_nsec == 1000) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    struct linux_itimerspec set1 = {{1,0},{2,0}};
    (void)linux_timer_settime(id, 0, &set1, NULL);
    struct linux_itimerspec set2 = {{3,0},{4,0}};
    struct linux_itimerspec old = {{0,0},{0,0}};
    int64_t r = linux_timer_settime(id, 0, &set2, &old);
    TEST("timer_settime old_value returns previous");
    if (r == 0 && old.it_interval.tv_sec == 1 && old.it_value.tv_sec == 2)
        PASS();
    else FAIL("");
}
static void t15(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    TEST("timer_gettime NULL -> -EFAULT");
    if (linux_timer_gettime(id, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    TEST("timer_getoverrun fresh -> 0");
    if (linux_timer_getoverrun(id) == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    linux_posix_timer_reset_for_tests();
    int id;
    (void)linux_timer_create(LINUX_CLOCK_MONOTONIC, NULL, &id);
    int64_t r = linux_timer_delete(id);
    int64_t r2 = linux_timer_settime(id, 0, NULL, NULL);
    TEST("timer_delete then settime same id -> -EINVAL");
    if (r == 0 && r2 == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t18(void) {
    linux_posix_timer_reset_for_tests();
    TEST("timer_delete invalid id -> -EINVAL");
    if (linux_timer_delete(99) == -LINUX_EINVAL) PASS();
    else FAIL("");
}

int test_linux_posix_timer_run(void) {
    printf("[test_linux_posix_timer]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
