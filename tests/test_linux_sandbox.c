#include "kernel/linux_compat/linux_sandbox.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_chroot_calls;
static const char *g_last_path;
static int64_t g_chroot_rc;

static int64_t fake_chroot(const char *p) {
    g_chroot_calls++;
    g_last_path = p;
    return g_chroot_rc;
}

static void install_fake(int64_t rc) {
    static struct linux_sandbox_ops o;
    o.chroot = fake_chroot;
    g_chroot_calls = 0;
    g_last_path = NULL;
    g_chroot_rc = rc;
    linux_sandbox_reset_for_tests();
    linux_sandbox_install_ops(&o);
}

static void t1(void) {
    linux_sandbox_reset_for_tests();
    TEST("chroot(NULL) -> -EFAULT");
    if (linux_chroot(NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_sandbox_reset_for_tests();
    TEST("chroot(\"\") -> -ENOENT");
    if (linux_chroot("") == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_sandbox_reset_for_tests();
    TEST("chroot(\"/\") -> 0 (no provider)");
    if (linux_chroot("/") == 0) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake(0);
    int64_t rc = linux_chroot("/jail");
    TEST("chroot delegates to provider with path");
    if (rc == 0 && g_chroot_calls == 1 && g_last_path &&
        g_last_path[0] == '/' && g_last_path[1] == 'j') PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake(-LINUX_EACCES);
    TEST("chroot provider error forwarded");
    if (linux_chroot("/forbidden") == -LINUX_EACCES) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_sandbox_reset_for_tests();
    int64_t r1 = linux_personality(LINUX_PER_LINUX);
    TEST("personality default get -> 0 (PER_LINUX)");
    if (r1 == LINUX_PER_LINUX) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_sandbox_reset_for_tests();
    int64_t prev = linux_personality(LINUX_ADDR_NO_RANDOMIZE);
    int64_t cur  = linux_personality(LINUX_PERSONALITY_QUERY);
    TEST("personality set/query: prev was default, current is set");
    if (prev == 0 && (uint32_t)cur == LINUX_ADDR_NO_RANDOMIZE) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_sandbox_reset_for_tests();
    (void)linux_personality(LINUX_PER_LINUX_32BIT);
    int64_t prev = linux_personality(LINUX_PERSONALITY_QUERY);
    /* QUERY should NOT change the value. */
    int64_t cur  = linux_personality(LINUX_PERSONALITY_QUERY);
    TEST("personality(QUERY) is read-only (idempotent)");
    if (prev == cur && (uint32_t)cur == LINUX_PER_LINUX_32BIT) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_sandbox_reset_for_tests();
    int64_t prev = linux_setfsuid(1000);
    TEST("setfsuid first -> prev 0");
    if (prev == 0) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_sandbox_reset_for_tests();
    (void)linux_setfsuid(1000);
    int64_t prev = linux_setfsuid(2000);
    TEST("setfsuid second -> prev 1000");
    if (prev == 1000) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_sandbox_reset_for_tests();
    (void)linux_setfsuid(500);
    int64_t prev = linux_setfsuid(-1);
    /* -1 is the Linux probe sentinel: don't change, just
     * return current. */
    int64_t cur = linux_setfsuid(-1);
    TEST("setfsuid(-1) is a probe (returns current; no change)");
    if (prev == 500 && cur == 500) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_sandbox_reset_for_tests();
    int64_t prev = linux_setfsgid(2000);
    TEST("setfsgid first -> prev 0");
    if (prev == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_sandbox_reset_for_tests();
    (void)linux_setfsgid(2000);
    int64_t prev = linux_setfsgid(3000);
    TEST("setfsgid second -> prev 2000");
    if (prev == 2000) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake(-LINUX_EACCES);
    linux_sandbox_install_ops(NULL);
    int64_t rc = linux_chroot("/jail");
    TEST("sandbox install_ops(NULL) clears chroot callback");
    if (rc == 0 && g_chroot_calls == 0) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake(-LINUX_EACCES);
    linux_sandbox_reset_for_tests();
    int64_t rc = linux_chroot("/jail");
    TEST("sandbox reset clears installed callbacks");
    if (rc == 0 && g_chroot_calls == 0) PASS();
    else FAIL("");
}

int test_linux_sandbox_run(void) {
    printf("[test_linux_sandbox]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14(); t15();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
