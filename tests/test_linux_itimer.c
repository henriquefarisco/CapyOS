#include "kernel/linux_compat/linux_itimer.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int64_t g_now = 0;
static int g_now_calls;
static int64_t fake_now(void) { g_now_calls++; return g_now; }

static void install_fake(void) {
    static const struct linux_itimer_ops o = {
        .now_ticks = fake_now,
    };
    g_now = 0;
    g_now_calls = 0;
    linux_itimer_reset_for_tests();
    linux_itimer_install_ops(&o);
}

static void t1(void) {
    linux_itimer_reset_for_tests();
    TEST("alarm(0) on empty state -> 0");
    if (linux_alarm(0) == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_itimer_reset_for_tests();
    (void)linux_alarm(60);
    TEST("alarm(30) returns prev (60)");
    if (linux_alarm(30) == 60) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_itimer_reset_for_tests();
    (void)linux_alarm(45);
    TEST("alarm(0) cancels and returns prev (45)");
    if (linux_alarm(0) == 45) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_itimer_reset_for_tests();
    TEST("getitimer invalid which -> -EINVAL");
    struct linux_itimerval c;
    if (linux_getitimer(99, &c) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_itimer_reset_for_tests();
    TEST("getitimer NULL -> -EFAULT");
    if (linux_getitimer(LINUX_ITIMER_REAL, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval c = { .it_interval = {99, 99},
                                 .it_value = {99, 99} };
    int64_t rc = linux_getitimer(LINUX_ITIMER_REAL, &c);
    TEST("getitimer fresh -> all zero");
    if (rc == 0 &&
        c.it_interval.tv_sec == 0 && c.it_interval.tv_usec == 0 &&
        c.it_value.tv_sec == 0 && c.it_value.tv_usec == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_itimer_reset_for_tests();
    TEST("setitimer NULL new -> -EFAULT");
    if (linux_setitimer(LINUX_ITIMER_REAL, NULL, NULL) == -LINUX_EFAULT)
        PASS();
    else FAIL("");
}
static void t8(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval n = { .it_interval = {0, 1000000},
                                 .it_value = {1, 0} };
    TEST("setitimer tv_usec >= 1000000 -> -EINVAL");
    if (linux_setitimer(LINUX_ITIMER_REAL, &n, NULL) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t9(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval n = { .it_interval = {-1, 0},
                                 .it_value = {1, 0} };
    TEST("setitimer negative tv_sec -> -EINVAL");
    if (linux_setitimer(LINUX_ITIMER_REAL, &n, NULL) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t10(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval n = { .it_interval = {1, 500},
                                 .it_value = {2, 1000} };
    int64_t rc = linux_setitimer(LINUX_ITIMER_REAL, &n, NULL);
    TEST("setitimer + getitimer round-trip");
    struct linux_itimerval c;
    int64_t rc2 = linux_getitimer(LINUX_ITIMER_REAL, &c);
    if (rc == 0 && rc2 == 0 &&
        c.it_interval.tv_sec == 1 && c.it_interval.tv_usec == 500 &&
        c.it_value.tv_sec == 2 && c.it_value.tv_usec == 1000) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval n1 = { {1,0}, {2,0} };
    struct linux_itimerval n2 = { {3,0}, {4,0} };
    (void)linux_setitimer(LINUX_ITIMER_VIRTUAL, &n1, NULL);
    struct linux_itimerval old = {{0,0},{0,0}};
    int64_t rc = linux_setitimer(LINUX_ITIMER_VIRTUAL, &n2, &old);
    TEST("setitimer with old_value returns previous");
    if (rc == 0 && old.it_interval.tv_sec == 1 && old.it_value.tv_sec == 2)
        PASS();
    else FAIL("");
}
static void t12(void) {
    linux_itimer_reset_for_tests();
    struct linux_itimerval n_real  = { {1,0}, {2,0} };
    struct linux_itimerval n_virt  = { {3,0}, {4,0} };
    struct linux_itimerval n_prof  = { {5,0}, {6,0} };
    (void)linux_setitimer(LINUX_ITIMER_REAL, &n_real, NULL);
    (void)linux_setitimer(LINUX_ITIMER_VIRTUAL, &n_virt, NULL);
    (void)linux_setitimer(LINUX_ITIMER_PROF, &n_prof, NULL);
    struct linux_itimerval r, v, p;
    (void)linux_getitimer(LINUX_ITIMER_REAL, &r);
    (void)linux_getitimer(LINUX_ITIMER_VIRTUAL, &v);
    (void)linux_getitimer(LINUX_ITIMER_PROF, &p);
    TEST("itimers are independent (3 slots)");
    if (r.it_value.tv_sec == 2 && v.it_value.tv_sec == 4 &&
        p.it_value.tv_sec == 6) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_itimer_reset_for_tests();
    int64_t rc = linux_times(NULL);
    TEST("times(NULL) without provider -> 0 ticks");
    if (rc == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_itimer_reset_for_tests();
    struct linux_tms b = { .tms_utime = 99, .tms_stime = 99,
                           .tms_cutime = 99, .tms_cstime = 99 };
    int64_t rc = linux_times(&b);
    TEST("times(buf) without provider -> all zero");
    if (rc == 0 && b.tms_utime == 0 && b.tms_stime == 0 &&
        b.tms_cutime == 0 && b.tms_cstime == 0) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    g_now = 12345;
    int64_t rc = linux_times(NULL);
    TEST("times(NULL) with provider -> ticks from provider");
    if (rc == 12345 && g_now_calls == 1) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    g_now = 67890;
    struct linux_tms b;
    int64_t rc = linux_times(&b);
    TEST("times(buf) with provider populates ticks + zero per-task");
    if (rc == 67890 && b.tms_utime == 0 && b.tms_stime == 0 &&
        g_now_calls == 1) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    g_now = 12345;
    linux_itimer_install_ops(NULL);
    int64_t rc = linux_times(NULL);
    TEST("itimer install_ops(NULL) clears now_ticks callback");
    if (rc == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    g_now = 12345;
    linux_itimer_reset_for_tests();
    int64_t rc = linux_times(NULL);
    TEST("itimer reset clears installed callbacks");
    if (rc == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}

int test_linux_itimer_run(void) {
    printf("[test_linux_itimer]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16();
    t17(); t18();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
