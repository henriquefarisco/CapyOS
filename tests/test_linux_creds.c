#include "kernel/linux_compat/linux_creds.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* --- getgroups --- */

static void t_getgroups_negative_einval(void) {
    TEST("getgroups(-1, NULL) -> -EINVAL");
    if (linux_getgroups(-1, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_getgroups_query_count(void) {
    /* Linux idiom: getgroups(0, NULL) returns the count without
     * touching the buffer. We have no supplementary groups so
     * the count is 0. */
    TEST("getgroups(0, NULL) -> 0 (zero supplementary groups)");
    if (linux_getgroups(0, NULL) == 0) PASS();
    else FAIL("count query wrong");
}

static void t_getgroups_size_positive_null_efault(void) {
    TEST("getgroups(8, NULL) -> -EFAULT");
    if (linux_getgroups(8, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_getgroups_size_positive_valid_buf(void) {
    uint32_t buf[8] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
    int64_t r = linux_getgroups(8, buf);
    TEST("getgroups(8, buf) -> 0 (no groups; buf untouched)");
    if (r == 0 && buf[0] == 0xCC) PASS();
    else FAIL("buf modified");
}

/* --- setgroups --- */

static void t_setgroups_too_many_einval(void) {
    TEST("setgroups(NGROUPS_MAX+1, list) -> -EINVAL");
    static uint32_t big[1] = { 0 };
    if (linux_setgroups((size_t)LINUX_NGROUPS_MAX + 1, big)
        == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_setgroups_size_positive_null_efault(void) {
    TEST("setgroups(4, NULL) -> -EFAULT");
    if (linux_setgroups(4, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_setgroups_zero_size_no_buf_ok(void) {
    TEST("setgroups(0, NULL) -> 0 (size=0 doesn't need buffer)");
    if (linux_setgroups(0, NULL) == 0) PASS();
    else FAIL("zero size rejected");
}

static void t_setgroups_normal_ok(void) {
    uint32_t list[2] = { 1000, 1001 };
    TEST("setgroups(2, list) -> 0 (root has CAP_SETGID; no-op)");
    if (linux_setgroups(2, list) == 0) PASS();
    else FAIL("normal setgroups failed");
}

int test_linux_creds_run(void) {
    printf("[test_linux_creds]\n");
    tests_run = tests_passed = 0;

    t_getgroups_negative_einval();
    t_getgroups_query_count();
    t_getgroups_size_positive_null_efault();
    t_getgroups_size_positive_valid_buf();

    t_setgroups_too_many_einval();
    t_setgroups_size_positive_null_efault();
    t_setgroups_zero_size_no_buf_ok();
    t_setgroups_normal_ok();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
