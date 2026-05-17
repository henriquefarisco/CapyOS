#include "kernel/linux_compat/linux_link.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_hard_calls;
static int g_sym_calls;
static int g_last_follow;
static char g_last_old[128];
static char g_last_new[128];
static char g_last_target[128];

static void cap(char *d, const char *s) {
    size_t i = 0;
    while (i < 127 && s && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int64_t fake_hard(const char *o, const char *n, int f) {
    g_hard_calls++; cap(g_last_old, o); cap(g_last_new, n);
    g_last_follow = f; return 0;
}
static int64_t fake_sym(const char *t, const char *l) {
    g_sym_calls++; cap(g_last_target, t); cap(g_last_new, l); return 0;
}

static void install_fake(void) {
    static const struct linux_link_ops o = {
        .hard_link = fake_hard,
        .sym_link  = fake_sym,
    };
    g_hard_calls = g_sym_calls = 0;
    g_last_follow = -1;
    g_last_old[0] = g_last_new[0] = g_last_target[0] = '\0';
    linux_link_reset_for_tests();
    linux_link_install_ops(&o);
}

static void t1(void) {
    install_fake();
    TEST("link(NULL, \"/n\") -> -EFAULT");
    if (linux_link(NULL, "/n") == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    install_fake();
    TEST("link(\"/o\", \"\") -> -ENOENT");
    if (linux_link("/o", "") == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_link_reset_for_tests();
    TEST("link without ops -> -ENOSYS");
    if (linux_link("/o", "/n") == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake();
    int64_t r = linux_link("/o", "/n");
    TEST("link calls hard_link with follow=0");
    if (r == 0 && g_hard_calls == 1 && g_last_follow == 0 &&
        strcmp(g_last_old, "/o") == 0 &&
        strcmp(g_last_new, "/n") == 0) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    int64_t r = linux_linkat(LINUX_LINK_AT_FDCWD, "/o",
                             LINUX_LINK_AT_FDCWD, "/n",
                             LINUX_LINK_AT_SYMLINK_FOLLOW);
    TEST("linkat with AT_SYMLINK_FOLLOW -> follow=1");
    if (r == 0 && g_hard_calls == 1 && g_last_follow == 1) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake();
    TEST("linkat unknown flag -> -EINVAL");
    if (linux_linkat(LINUX_LINK_AT_FDCWD, "/o",
                     LINUX_LINK_AT_FDCWD, "/n", 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    TEST("linkat(7,...,8,...) -> -ENOTDIR");
    if (linux_linkat(7, "/o", 8, "/n", 0) == -LINUX_ENOTDIR) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    TEST("symlink(NULL, \"/l\") -> -EFAULT");
    if (linux_symlink(NULL, "/l") == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    TEST("symlink(\"\", \"/l\") -> -ENOENT (empty target)");
    if (linux_symlink("", "/l") == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    int64_t r = linux_symlink("/target", "/link");
    TEST("symlink calls sym_link provider");
    if (r == 0 && g_sym_calls == 1 &&
        strcmp(g_last_target, "/target") == 0 &&
        strcmp(g_last_new, "/link") == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    TEST("symlinkat(target, 7, link) -> -ENOTDIR");
    if (linux_symlinkat("/t", 7, "/l") == -LINUX_ENOTDIR) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_link_reset_for_tests();
    TEST("symlink without ops -> -ENOSYS");
    if (linux_symlink("/t", "/l") == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t13(void) {
    install_fake();
    linux_link_install_ops(NULL);
    int64_t r1 = linux_link("/o", "/n");
    int64_t r2 = linux_symlink("/t", "/l");
    TEST("link install_ops(NULL) clears link callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_hard_calls == 0 && g_sym_calls == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    linux_link_reset_for_tests();
    int64_t r1 = linux_link("/o", "/n");
    int64_t r2 = linux_symlink("/t", "/l");
    TEST("link reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_hard_calls == 0 && g_sym_calls == 0) PASS();
    else FAIL("");
}

int test_linux_link_run(void) {
    printf("[test_linux_link]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
