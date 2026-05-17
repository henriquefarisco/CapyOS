#include "kernel/linux_compat/linux_trunc.h"
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

/* --- ops fixture --- */

static int     g_ftrunc_calls = 0;
static int     g_last_fd      = -1;
static int64_t g_last_len     = -1;
static int64_t g_next_rc      = 0;

static int64_t fake_ftruncate(int fd, int64_t length) {
    g_ftrunc_calls++;
    g_last_fd = fd;
    g_last_len = length;
    return g_next_rc;
}
static void install_fake(void) {
    static const struct linux_trunc_ops ops = {
        .ftruncate = fake_ftruncate,
    };
    g_ftrunc_calls = 0;
    g_last_fd = -1;
    g_last_len = -1;
    g_next_rc = 0;
    linux_trunc_reset_for_tests();
    linux_trunc_install_ops(&ops);
}

/* --- truncate --- */

static void t_truncate_null_path_efault(void) {
    TEST("truncate(NULL, 0) -> -EFAULT");
    if (linux_truncate(NULL, 0) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_truncate_empty_path_enoent(void) {
    TEST("truncate(\"\", 0) -> -ENOENT");
    if (linux_truncate("", 0) == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_truncate_negative_length_einval(void) {
    TEST("truncate(\"/foo\", -1) -> -EINVAL");
    if (linux_truncate("/foo", -1) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_truncate_path_enosys(void) {
    TEST("truncate(\"/foo\", 100) -> -ENOSYS");
    if (linux_truncate("/foo", 100) == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

/* --- ftruncate --- */

static void t_ftruncate_negative_fd_ebadf(void) {
    install_fake();
    TEST("ftruncate(-1, 0) -> -EBADF");
    if (linux_ftruncate(-1, 0) == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_ftruncate_negative_length_einval(void) {
    install_fake();
    TEST("ftruncate(3, -1) -> -EINVAL");
    if (linux_ftruncate(3, -1) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_ftruncate_no_ops_enosys(void) {
    linux_trunc_reset_for_tests();
    TEST("ftruncate(3, 100) without ops -> -ENOSYS");
    if (linux_ftruncate(3, 100) == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_ftruncate_calls_ops(void) {
    install_fake();
    int64_t r = linux_ftruncate(7, 4096);
    TEST("ftruncate(7, 4096) -> ops.ftruncate invoked");
    if (r == 0 && g_ftrunc_calls == 1 &&
        g_last_fd == 7 && g_last_len == 4096) PASS();
    else FAIL("ops not invoked");
}

static void t_ftruncate_ops_error_forwarded(void) {
    install_fake();
    g_next_rc = -LINUX_ENOSPC;
    TEST("ftruncate ops returns -ENOSPC -> forwarded");
    if (linux_ftruncate(7, 1 << 30) == -LINUX_ENOSPC) PASS();
    else FAIL("ENOSPC not forwarded");
}

static void t_ftruncate_install_null_clears(void) {
    install_fake();
    linux_trunc_install_ops(NULL);
    TEST("install_ops(NULL) clears -> ftruncate falls back to ENOSYS");
    if (linux_ftruncate(3, 0) == -LINUX_ENOSYS && g_ftrunc_calls == 0) PASS();
    else FAIL("install(NULL) didn't clear");
}

static void t_ftruncate_reset_clears(void) {
    install_fake();
    linux_trunc_reset_for_tests();
    TEST("trunc reset clears installed callbacks");
    if (linux_ftruncate(3, 0) == -LINUX_ENOSYS && g_ftrunc_calls == 0) PASS();
    else FAIL("reset didn't clear");
}

int test_linux_trunc_run(void) {
    printf("[test_linux_trunc]\n");
    tests_run = tests_passed = 0;

    t_truncate_null_path_efault();
    t_truncate_empty_path_enoent();
    t_truncate_negative_length_einval();
    t_truncate_path_enosys();

    t_ftruncate_negative_fd_ebadf();
    t_ftruncate_negative_length_einval();
    t_ftruncate_no_ops_enosys();
    t_ftruncate_calls_ops();
    t_ftruncate_ops_error_forwarded();
    t_ftruncate_install_null_clears();
    t_ftruncate_reset_clears();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
