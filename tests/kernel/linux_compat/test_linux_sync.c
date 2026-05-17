#include "kernel/linux_compat/linux_sync.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_sync_all_calls;
static int g_sync_fs_calls;
static int g_sync_fd_calls;
static int g_last_fd;
static int g_last_data_only;

static int64_t fake_sync_all(void) { g_sync_all_calls++; return 0; }
static int64_t fake_sync_fs(int fd) {
    g_sync_fs_calls++; g_last_fd = fd; return 0;
}
static int64_t fake_sync_fd(int fd, int data_only) {
    g_sync_fd_calls++; g_last_fd = fd; g_last_data_only = data_only;
    return 0;
}

static void install_fake(void) {
    static const struct linux_sync_ops o = {
        .sync_all = fake_sync_all,
        .sync_fs  = fake_sync_fs,
        .sync_fd  = fake_sync_fd,
    };
    g_sync_all_calls = g_sync_fs_calls = g_sync_fd_calls = 0;
    g_last_fd = -1; g_last_data_only = -1;
    linux_sync_reset_for_tests();
    linux_sync_install_ops(&o);
}

static void t1(void) {
    linux_sync_reset_for_tests();
    TEST("sync() without ops -> 0 (no-op success)");
    if (linux_sync() == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    install_fake();
    int64_t r = linux_sync();
    TEST("sync() with ops -> sync_all called");
    if (r == 0 && g_sync_all_calls == 1) PASS();
    else FAIL("");
}
static void t3(void) {
    install_fake();
    TEST("syncfs(-1) -> -EBADF");
    if (linux_syncfs(-1) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_sync_reset_for_tests();
    TEST("syncfs(7) without ops -> 0");
    if (linux_syncfs(7) == 0) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    int64_t r = linux_syncfs(7);
    TEST("syncfs(7) -> sync_fs(7)");
    if (r == 0 && g_sync_fs_calls == 1 && g_last_fd == 7) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake();
    TEST("fsync(-1) -> -EBADF");
    if (linux_fsync(-1) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    int64_t r = linux_fsync(7);
    TEST("fsync(7) -> sync_fd(7, data_only=0)");
    if (r == 0 && g_sync_fd_calls == 1 &&
        g_last_fd == 7 && g_last_data_only == 0) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    int64_t r = linux_fdatasync(7);
    TEST("fdatasync(7) -> sync_fd(7, data_only=1)");
    if (r == 0 && g_sync_fd_calls == 1 &&
        g_last_fd == 7 && g_last_data_only == 1) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    TEST("fdatasync(-1) -> -EBADF");
    if (linux_fdatasync(-1) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_sync_reset_for_tests();
    TEST("fsync(7) without ops -> 0 (RAM-only durability)");
    if (linux_fsync(7) == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    linux_sync_install_ops(NULL);
    int64_t r1 = linux_sync();
    int64_t r2 = linux_syncfs(7);
    int64_t r3 = linux_fsync(7);
    int64_t r4 = linux_fdatasync(7);
    TEST("sync install_ops(NULL) clears durability callbacks");
    if (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0 &&
        g_sync_all_calls == 0 && g_sync_fs_calls == 0 &&
        g_sync_fd_calls == 0) PASS();
    else FAIL("");
}
static void t12(void) {
    install_fake();
    linux_sync_reset_for_tests();
    int64_t r1 = linux_sync();
    int64_t r2 = linux_syncfs(7);
    int64_t r3 = linux_fsync(7);
    int64_t r4 = linux_fdatasync(7);
    TEST("sync reset clears installed callbacks");
    if (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0 &&
        g_sync_all_calls == 0 && g_sync_fs_calls == 0 &&
        g_sync_fd_calls == 0) PASS();
    else FAIL("");
}

int test_linux_sync_run(void) {
    printf("[test_linux_sync]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6();
    t7(); t8(); t9(); t10(); t11(); t12();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
