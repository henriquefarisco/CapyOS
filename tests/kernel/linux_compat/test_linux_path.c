#include "kernel/linux_compat/linux_path.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* --- Fake provider for /proc/self/exe --- */

static int g_provider_calls = 0;
static int64_t fake_resolve_exe(char *buf, size_t bufsize) {
    g_provider_calls++;
    const char *exe = "/usr/bin/capyjs";
    size_t n = 0;
    while (exe[n] && n < bufsize) { buf[n] = exe[n]; n++; }
    return (int64_t)n;
}

static void install_fake_provider(void) {
    static const struct linux_path_providers p = {
        .resolve_proc_self_exe = fake_resolve_exe,
    };
    g_provider_calls = 0;
    linux_path_reset_for_tests();
    linux_path_install(&p);
}

/* --- getcwd --- */

static void t_getcwd_null_buf(void) {
    int64_t r = linux_getcwd(NULL, 16);
    TEST("getcwd(NULL, 16) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_getcwd_zero_size(void) {
    char buf[16];
    int64_t r = linux_getcwd(buf, 0);
    TEST("getcwd(buf, 0) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_getcwd_size_one_erange(void) {
    char buf[2] = {'X', 'X'};
    int64_t r = linux_getcwd(buf, 1);
    TEST("getcwd(buf, 1) -> -ERANGE (need 2 bytes for / + NUL)");
    if (r == -LINUX_ERANGE && buf[0] == 'X') PASS();
    else FAIL("ERANGE not surfaced or buf clobbered");
}

static void t_getcwd_minimum_size(void) {
    char buf[2] = {'X', 'X'};
    int64_t r = linux_getcwd(buf, 2);
    TEST("getcwd(buf, 2) writes \"/\\0\" and returns 2");
    if (r == 2 && buf[0] == '/' && buf[1] == '\0') PASS();
    else FAIL("minimum buf wrong");
}

static void t_getcwd_large_buf(void) {
    char buf[64];
    memset(buf, 0xAA, sizeof(buf));
    int64_t r = linux_getcwd(buf, sizeof(buf));
    TEST("getcwd(buf, 64) writes \"/\\0\" and returns 2");
    if (r == 2 && buf[0] == '/' && buf[1] == '\0' &&
        (uint8_t)buf[2] == 0xAA) PASS();
    else FAIL("large buf wrong");
}

/* --- readlink --- */

static void t_readlink_null_path(void) {
    char buf[16];
    int64_t r = linux_readlink(NULL, buf, sizeof(buf));
    TEST("readlink(NULL, ..., ...) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced for NULL path");
}

static void t_readlink_null_buf(void) {
    int64_t r = linux_readlink("/foo", NULL, 16);
    TEST("readlink(\"/foo\", NULL, 16) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced for NULL buf");
}

static void t_readlink_zero_bufsize(void) {
    char buf[1];
    int64_t r = linux_readlink("/foo", buf, 0);
    TEST("readlink(..., bufsize=0) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_readlink_proc_self_exe_calls_provider(void) {
    install_fake_provider();
    char buf[64] = {0};
    int64_t r = linux_readlink("/proc/self/exe", buf, sizeof(buf));
    TEST("readlink(/proc/self/exe) -> calls provider, returns byte count");
    if (r == 15 /* "/usr/bin/capyjs" */ &&
        g_provider_calls == 1 &&
        buf[0] == '/' && buf[14] == 's') PASS();
    else FAIL("provider not invoked or bytes wrong");
}

static void t_readlink_proc_self_exe_no_provider(void) {
    linux_path_reset_for_tests();
    char buf[64];
    int64_t r = linux_readlink("/proc/self/exe", buf, sizeof(buf));
    TEST("readlink(/proc/self/exe) without provider -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_install_null_clears_readlink_provider(void) {
    install_fake_provider();
    linux_path_install(NULL);
    char buf[64];
    int64_t r = linux_readlink("/proc/self/exe", buf, sizeof(buf));
    TEST("path install(NULL) clears readlink /proc/self/exe provider");
    if (r == -LINUX_ENOSYS && g_provider_calls == 0) PASS();
    else FAIL("readlink provider not cleared");
}

static void t_reset_clears_readlink_provider(void) {
    install_fake_provider();
    linux_path_reset_for_tests();
    char buf[64];
    int64_t r = linux_readlink("/proc/self/exe", buf, sizeof(buf));
    TEST("path reset clears readlink /proc/self/exe provider");
    if (r == -LINUX_ENOSYS && g_provider_calls == 0) PASS();
    else FAIL("reset provider not cleared");
}

static void t_readlink_other_path_einval(void) {
    install_fake_provider();
    char buf[16];
    int64_t r = linux_readlink("/etc/hosts", buf, sizeof(buf));
    TEST("readlink(non-/proc/self/exe) -> -EINVAL (not a symlink)");
    if (r == -LINUX_EINVAL && g_provider_calls == 0) PASS();
    else FAIL("EINVAL not surfaced or provider called");
}

/* --- readlinkat --- */

static void t_readlinkat_at_fdcwd_delegates(void) {
    install_fake_provider();
    char buf[64];
    int64_t r = linux_readlinkat(LINUX_PATH_AT_FDCWD,
                                 "/proc/self/exe",
                                 buf, sizeof(buf));
    TEST("readlinkat(AT_FDCWD, /proc/self/exe) -> delegates to readlink");
    if (r == 15 && g_provider_calls == 1) PASS();
    else FAIL("AT_FDCWD didn't delegate");
}

static void t_install_null_clears_readlinkat_provider(void) {
    install_fake_provider();
    linux_path_install(NULL);
    char buf[64];
    int64_t r = linux_readlinkat(LINUX_PATH_AT_FDCWD,
                                 "/proc/self/exe",
                                 buf, sizeof(buf));
    TEST("path install(NULL) clears readlinkat /proc/self/exe provider");
    if (r == -LINUX_ENOSYS && g_provider_calls == 0) PASS();
    else FAIL("readlinkat provider not cleared");
}

static void t_readlinkat_other_dirfd_enotdir(void) {
    char buf[16];
    int64_t r = linux_readlinkat(3, "/proc/self/exe", buf, sizeof(buf));
    TEST("readlinkat(dirfd=3, ...) -> -ENOTDIR (no dir fd table)");
    if (r == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_readlinkat_at_fdcwd_other_path_einval(void) {
    install_fake_provider();
    char buf[16];
    int64_t r = linux_readlinkat(LINUX_PATH_AT_FDCWD,
                                 "/etc/hosts", buf, sizeof(buf));
    TEST("readlinkat(AT_FDCWD, non-symlink) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

int test_linux_path_run(void) {
    printf("[test_linux_path]\n");
    tests_run = tests_passed = 0;

    t_getcwd_null_buf();
    t_getcwd_zero_size();
    t_getcwd_size_one_erange();
    t_getcwd_minimum_size();
    t_getcwd_large_buf();

    t_readlink_null_path();
    t_readlink_null_buf();
    t_readlink_zero_bufsize();
    t_readlink_proc_self_exe_calls_provider();
    t_readlink_proc_self_exe_no_provider();
    t_install_null_clears_readlink_provider();
    t_reset_clears_readlink_provider();
    t_readlink_other_path_einval();

    t_readlinkat_at_fdcwd_delegates();
    t_install_null_clears_readlinkat_provider();
    t_readlinkat_other_dirfd_enotdir();
    t_readlinkat_at_fdcwd_other_path_einval();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
