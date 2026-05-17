#include "kernel/linux_compat/linux_fs_meta.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_chmod_path_calls;
static int g_chmod_fd_calls;
static int g_chown_path_calls;
static int g_chown_fd_calls;
static int g_last_fd;
static uint32_t g_last_mode;
static uint32_t g_last_uid;
static uint32_t g_last_gid;
static int      g_last_follow;

static int64_t fake_chmod_path(const char *p, uint32_t m) {
    (void)p; g_chmod_path_calls++; g_last_mode = m; return 0;
}
static int64_t fake_chmod_fd(int fd, uint32_t m) {
    g_chmod_fd_calls++; g_last_fd = fd; g_last_mode = m; return 0;
}
static int64_t fake_chown_path(const char *p, uint32_t u, uint32_t g, int f) {
    (void)p; g_chown_path_calls++;
    g_last_uid = u; g_last_gid = g; g_last_follow = f;
    return 0;
}
static int64_t fake_chown_fd(int fd, uint32_t u, uint32_t g) {
    g_chown_fd_calls++; g_last_fd = fd; g_last_uid = u; g_last_gid = g;
    return 0;
}

static void install_fake(void) {
    static const struct linux_fs_meta_ops o = {
        .chmod_path = fake_chmod_path,
        .chmod_fd   = fake_chmod_fd,
        .chown_path = fake_chown_path,
        .chown_fd   = fake_chown_fd,
    };
    g_chmod_path_calls = g_chmod_fd_calls = 0;
    g_chown_path_calls = g_chown_fd_calls = 0;
    g_last_fd = -1;
    g_last_mode = g_last_uid = g_last_gid = 0;
    g_last_follow = -1;
    linux_fs_meta_reset_for_tests();
    linux_fs_meta_install_ops(&o);
}

static void t1(void) {
    install_fake();
    TEST("chmod(NULL, 0644) -> -EFAULT");
    if (linux_chmod(NULL, 0644) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    install_fake();
    TEST("chmod(\"\", 0644) -> -ENOENT");
    if (linux_chmod("", 0644) == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_fs_meta_reset_for_tests();
    TEST("chmod without ops -> -ENOSYS");
    if (linux_chmod("/foo", 0644) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake();
    int64_t r = linux_chmod("/foo", 0xFFFFFFFFu);
    TEST("chmod clamps mode to 07777");
    if (r == 0 && g_last_mode == 07777) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    TEST("fchmod(-1, 0644) -> -EBADF");
    if (linux_fchmod(-1, 0644) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake();
    int64_t r = linux_fchmod(7, 0600);
    TEST("fchmod calls chmod_fd provider");
    if (r == 0 && g_chmod_fd_calls == 1 && g_last_fd == 7) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    TEST("fchmodat with non-zero flags -> -EINVAL");
    if (linux_fchmodat(LINUX_FS_META_AT_FDCWD, "/x", 0644, 0x100)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    int64_t r = linux_fchmodat(LINUX_FS_META_AT_FDCWD, "/x", 0644, 0);
    TEST("fchmodat(AT_FDCWD,...,0) delegates to chmod");
    if (r == 0 && g_chmod_path_calls == 1) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    TEST("fchmodat(7,...) -> -ENOTDIR");
    if (linux_fchmodat(7, "/x", 0644, 0) == -LINUX_ENOTDIR) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    TEST("chown(NULL, 0, 0) -> -EFAULT");
    if (linux_chown(NULL, 0, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    int64_t r = linux_chown("/x", 1000, 2000);
    TEST("chown calls chown_path with follow=1");
    if (r == 0 && g_chown_path_calls == 1 &&
        g_last_uid == 1000 && g_last_gid == 2000 &&
        g_last_follow == 1) PASS();
    else FAIL("");
}
static void t12(void) {
    install_fake();
    int64_t r = linux_lchown("/x", 1000, 2000);
    TEST("lchown calls chown_path with follow=0");
    if (r == 0 && g_chown_path_calls == 1 && g_last_follow == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    install_fake();
    TEST("fchown(-1, 0, 0) -> -EBADF");
    if (linux_fchown(-1, 0, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    int64_t r = linux_fchown(7, 1000, 2000);
    TEST("fchown calls chown_fd provider");
    if (r == 0 && g_chown_fd_calls == 1 && g_last_fd == 7) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    int64_t r = linux_fchownat(LINUX_FS_META_AT_FDCWD, "/x", 1000, 2000,
                               LINUX_FS_META_AT_SYMLINK_NOFOLLOW);
    TEST("fchownat with NOFOLLOW -> follow=0");
    if (r == 0 && g_chown_path_calls == 1 && g_last_follow == 0) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    TEST("fchownat unknown flag -> -EINVAL");
    if (linux_fchownat(LINUX_FS_META_AT_FDCWD, "/x", 0, 0, 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    TEST("fchownat AT_EMPTY_PATH with AT_FDCWD -> -EINVAL");
    if (linux_fchownat(LINUX_FS_META_AT_FDCWD, "", 0, 0,
                       LINUX_FS_META_AT_EMPTY_PATH)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    linux_fs_meta_install_ops(NULL);
    int64_t r1 = linux_chmod("/x", 0644);
    int64_t r2 = linux_fchmod(7, 0644);
    int64_t r3 = linux_chown("/x", 1, 2);
    int64_t r4 = linux_fchown(7, 1, 2);
    TEST("fs_meta install_ops(NULL) clears metadata callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        r3 == -LINUX_ENOSYS && r4 == -LINUX_ENOSYS &&
        g_chmod_path_calls == 0 && g_chmod_fd_calls == 0 &&
        g_chown_path_calls == 0 && g_chown_fd_calls == 0) PASS();
    else FAIL("");
}
static void t19(void) {
    install_fake();
    linux_fs_meta_reset_for_tests();
    int64_t r1 = linux_chmod("/x", 0644);
    int64_t r2 = linux_fchmod(7, 0644);
    int64_t r3 = linux_chown("/x", 1, 2);
    int64_t r4 = linux_fchown(7, 1, 2);
    TEST("fs_meta reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        r3 == -LINUX_ENOSYS && r4 == -LINUX_ENOSYS &&
        g_chmod_path_calls == 0 && g_chmod_fd_calls == 0 &&
        g_chown_path_calls == 0 && g_chown_fd_calls == 0) PASS();
    else FAIL("");
}

int test_linux_fs_meta_run(void) {
    printf("[test_linux_fs_meta]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
