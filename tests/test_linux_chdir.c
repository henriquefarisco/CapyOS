#include "kernel/linux_compat/linux_chdir.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_path_calls;
static int g_fd_calls;
static char g_last_path[128];
static int g_last_fd;
static int64_t g_next_rc;

static int64_t fake_path(const char *p) {
    g_path_calls++;
    size_t i = 0;
    while (i < 127 && p && p[i]) { g_last_path[i] = p[i]; i++; }
    g_last_path[i] = '\0';
    return g_next_rc;
}
static int64_t fake_fd(int fd) {
    g_fd_calls++; g_last_fd = fd;
    return g_next_rc;
}

static void install_fake(void) {
    static const struct linux_chdir_ops o = {
        .chdir_path = fake_path,
        .chdir_fd   = fake_fd,
    };
    g_path_calls = g_fd_calls = 0;
    g_last_path[0] = '\0'; g_last_fd = -1; g_next_rc = 0;
    linux_chdir_reset_for_tests();
    linux_chdir_install_ops(&o);
}

static void t1(void) {
    install_fake();
    TEST("chdir(NULL) -> -EFAULT");
    if (linux_chdir(NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    install_fake();
    TEST("chdir(\"\") -> -ENOENT");
    if (linux_chdir("") == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_chdir_reset_for_tests();
    TEST("chdir(\"/foo\") without ops -> -ENOSYS");
    if (linux_chdir("/foo") == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake();
    int64_t r = linux_chdir("/foo");
    TEST("chdir calls chdir_path provider");
    if (r == 0 && g_path_calls == 1 &&
        strcmp(g_last_path, "/foo") == 0) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    g_next_rc = -LINUX_EACCES;
    TEST("chdir provider error forwarded (-EACCES)");
    if (linux_chdir("/forbidden") == -LINUX_EACCES) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake();
    TEST("fchdir(-1) -> -EBADF");
    if (linux_fchdir(-1) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_chdir_reset_for_tests();
    TEST("fchdir(7) without ops -> -ENOSYS");
    if (linux_fchdir(7) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    int64_t r = linux_fchdir(7);
    TEST("fchdir calls chdir_fd provider");
    if (r == 0 && g_fd_calls == 1 && g_last_fd == 7) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    linux_chdir_install_ops(NULL);
    int64_t r1 = linux_chdir("/x");
    int64_t r2 = linux_fchdir(7);
    TEST("chdir install_ops(NULL) clears cwd callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_path_calls == 0 && g_fd_calls == 0) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    linux_chdir_reset_for_tests();
    int64_t r1 = linux_chdir("/x");
    int64_t r2 = linux_fchdir(7);
    TEST("chdir reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_path_calls == 0 && g_fd_calls == 0) PASS();
    else FAIL("");
}

int test_linux_chdir_run(void) {
    printf("[test_linux_chdir]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
