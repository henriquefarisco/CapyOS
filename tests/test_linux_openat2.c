#include "kernel/linux_compat/linux_openat2.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_open_calls;
static uint64_t g_last_resolve;
static int g_access_calls;
static int g_access_mode;
static int g_access_flags;

static int64_t fake_open(int dirfd, const char *path,
                         uint64_t flags, uint64_t mode,
                         uint64_t resolve) {
    (void)dirfd; (void)path; (void)flags; (void)mode;
    g_open_calls++; g_last_resolve = resolve;
    return 42; /* synthetic fd */
}
static int64_t fake_access(int dirfd, const char *path,
                           int mode, int flags) {
    (void)dirfd; (void)path;
    g_access_calls++; g_access_mode = mode; g_access_flags = flags;
    return 0;
}

static void install_fake(void) {
    static struct linux_openat2_ops o;
    o.openat = fake_open;
    o.faccessat = fake_access;
    g_open_calls = g_access_calls = 0;
    g_last_resolve = 0;
    g_access_mode = g_access_flags = -1;
    linux_openat2_reset_for_tests();
    linux_openat2_install_ops(&o);
}

static void t1(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 NULL path -> -EFAULT");
    struct linux_open_how h = {0, 0, 0};
    if (linux_openat2(LINUX_AT_FDCWD, NULL, &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 empty path -> -ENOENT");
    struct linux_open_how h = {0, 0, 0};
    if (linux_openat2(LINUX_AT_FDCWD, "", &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 NULL how -> -EFAULT");
    if (linux_openat2(LINUX_AT_FDCWD, "/x", NULL,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 size<24 -> -EINVAL");
    struct linux_open_how h = {0, 0, 0};
    if (linux_openat2(LINUX_AT_FDCWD, "/x", &h, 16) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t5(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 unknown resolve bits -> -EINVAL");
    struct linux_open_how h = {0, 0, 0x1000};
    if (linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 BENEATH | IN_ROOT -> -EINVAL (mutually exclusive)");
    struct linux_open_how h = {
        .resolve = LINUX_RESOLVE_BENEATH | LINUX_RESOLVE_IN_ROOT
    };
    if (linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 dirfd<0 not AT_FDCWD -> -EBADF");
    struct linux_open_how h = {0, 0, 0};
    if (linux_openat2(-50, "/x", &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_openat2_reset_for_tests();
    TEST("openat2 valid w/o ops -> -ENOSYS");
    struct linux_open_how h = {.resolve = LINUX_RESOLVE_BENEATH};
    if (linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                      LINUX_OPEN_HOW_SIZE_VER0) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    struct linux_open_how h = {.resolve = LINUX_RESOLVE_NO_SYMLINKS};
    int64_t r = linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                              LINUX_OPEN_HOW_SIZE_VER0);
    TEST("openat2 with ops delegates with resolve flags");
    if (r == 42 && g_open_calls == 1 &&
        g_last_resolve == LINUX_RESOLVE_NO_SYMLINKS) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    /* Linux: kernel zero-extends from `size` to its understanding;
     * larger sizes are accepted. */
    struct linux_open_how h = {.resolve = 0};
    int64_t r = linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                              LINUX_OPEN_HOW_SIZE_VER0 + 32);
    TEST("openat2 size>min accepted (kernel zero-extends)");
    if (r == 42) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 NULL path no AT_EMPTY_PATH -> -EFAULT");
    if (linux_faccessat2(LINUX_AT_FDCWD, NULL, LINUX_F_OK, 0)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 empty path no AT_EMPTY_PATH -> -ENOENT");
    if (linux_faccessat2(LINUX_AT_FDCWD, "", LINUX_F_OK, 0)
        == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 unknown mode bit -> -EINVAL");
    if (linux_faccessat2(LINUX_AT_FDCWD, "/x", 0x80, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 unknown flag bit -> -EINVAL");
    if (linux_faccessat2(LINUX_AT_FDCWD, "/x", LINUX_R_OK, 0x800)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 dirfd<0 not AT_FDCWD -> -EBADF");
    if (linux_faccessat2(-50, "/x", LINUX_F_OK, 0) == -LINUX_EBADF)
        PASS();
    else FAIL("");
}
static void t16(void) {
    linux_openat2_reset_for_tests();
    TEST("faccessat2 well-formed without ops -> 0 (single-root)");
    if (linux_faccessat2(LINUX_AT_FDCWD, "/x",
                         LINUX_R_OK | LINUX_W_OK,
                         LINUX_AT_EACCESS) == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    int64_t r = linux_faccessat2(LINUX_AT_FDCWD, "/x",
                                 LINUX_R_OK,
                                 LINUX_AT_SYMLINK_NOFOLLOW);
    TEST("faccessat2 with ops delegates");
    if (r == 0 && g_access_calls == 1 &&
        g_access_mode == LINUX_R_OK &&
        g_access_flags == LINUX_AT_SYMLINK_NOFOLLOW) PASS();
    else FAIL("");
}

static void t18(void) {
    install_fake();
    linux_openat2_install_ops(NULL);
    struct linux_open_how h = {.resolve = LINUX_RESOLVE_BENEATH};
    int64_t r = linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                              LINUX_OPEN_HOW_SIZE_VER0);
    TEST("openat2 install_ops(NULL) clears openat callback");
    if (r == -LINUX_ENOSYS && g_open_calls == 0) PASS();
    else FAIL("");
}

static void t19(void) {
    install_fake();
    linux_openat2_install_ops(NULL);
    int64_t r = linux_faccessat2(LINUX_AT_FDCWD, "/x", LINUX_R_OK,
                                 LINUX_AT_EACCESS);
    TEST("openat2 install_ops(NULL) clears faccessat2 callback");
    if (r == 0 && g_access_calls == 0) PASS();
    else FAIL("");
}

static void t20(void) {
    install_fake();
    linux_openat2_reset_for_tests();
    struct linux_open_how h = {.resolve = LINUX_RESOLVE_NO_SYMLINKS};
    int64_t r1 = linux_openat2(LINUX_AT_FDCWD, "/x", &h,
                               LINUX_OPEN_HOW_SIZE_VER0);
    int64_t r2 = linux_faccessat2(LINUX_AT_FDCWD, "/x", LINUX_R_OK, 0);
    TEST("openat2 reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == 0 &&
        g_open_calls == 0 && g_access_calls == 0) PASS();
    else FAIL("");
}

int test_linux_openat2_run(void) {
    printf("[test_linux_openat2]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17();
    t18(); t19(); t20();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
