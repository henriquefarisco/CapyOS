#include "kernel/linux_compat/linux_setid.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    TEST("setuid(0) -> 0 (we are root)");
    if (linux_setuid(0) == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    TEST("setuid(1000) -> -EPERM (Marco M1: root only)");
    if (linux_setuid(1000) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t3(void) {
    TEST("setgid(0) -> 0");
    if (linux_setgid(0) == 0) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("setgid(2000) -> -EPERM");
    if (linux_setgid(2000) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t5(void) {
    TEST("setresuid(0, 0, 0) -> 0");
    if (linux_setresuid(0, 0, 0) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    TEST("setresuid(-1, -1, -1) -> 0 (no-change all components)");
    if (linux_setresuid(LINUX_SETID_UID_NOCHANGE,
                        LINUX_SETID_UID_NOCHANGE,
                        LINUX_SETID_UID_NOCHANGE) == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    TEST("setresuid(0, -1, 0) -> 0 (mixed: change r, keep e, change s)");
    if (linux_setresuid(0, LINUX_SETID_UID_NOCHANGE, 0) == 0) PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("setresuid(1000, 0, 0) -> -EPERM (real uid != root)");
    if (linux_setresuid(1000, 0, 0) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t9(void) {
    TEST("setresuid(0, 1000, 0) -> -EPERM (effective uid != root)");
    if (linux_setresuid(0, 1000, 0) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t10(void) {
    TEST("setresgid(0, 0, 0) -> 0");
    if (linux_setresgid(0, 0, 0) == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("setresgid(-1, -1, -1) -> 0");
    if (linux_setresgid(LINUX_SETID_UID_NOCHANGE,
                        LINUX_SETID_UID_NOCHANGE,
                        LINUX_SETID_UID_NOCHANGE) == 0) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("setresgid(0, 0, 99) -> -EPERM (saved gid != root)");
    if (linux_setresgid(0, 0, 99) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t13(void) {
    uint32_t r, e, s;
    int64_t rc = linux_getresuid(&r, &e, &s);
    TEST("getresuid -> (0, 0, 0)");
    if (rc == 0 && r == 0 && e == 0 && s == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    uint32_t e, s;
    TEST("getresuid(NULL, &e, &s) -> -EFAULT");
    if (linux_getresuid(NULL, &e, &s) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t15(void) {
    uint32_t r, s;
    TEST("getresuid(&r, NULL, &s) -> -EFAULT");
    if (linux_getresuid(&r, NULL, &s) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t16(void) {
    uint32_t r, e;
    TEST("getresuid(&r, &e, NULL) -> -EFAULT");
    if (linux_getresuid(&r, &e, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t17(void) {
    uint32_t r, e, s;
    int64_t rc = linux_getresgid(&r, &e, &s);
    TEST("getresgid -> (0, 0, 0)");
    if (rc == 0 && r == 0 && e == 0 && s == 0) PASS();
    else FAIL("");
}
static void t18(void) {
    uint32_t e, s;
    TEST("getresgid(NULL, &e, &s) -> -EFAULT");
    if (linux_getresgid(NULL, &e, &s) == -LINUX_EFAULT) PASS();
    else FAIL("");
}

int test_linux_setid_run(void) {
    printf("[test_linux_setid]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
