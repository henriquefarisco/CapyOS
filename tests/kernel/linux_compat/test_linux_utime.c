#include "kernel/linux_compat/linux_utime.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_path_calls;
static int g_fd_calls;
static int g_now_calls;
static struct linux_timespec g_last_a;
static struct linux_timespec g_last_m;
static int g_last_follow;
static int g_last_fd;

static int64_t fake_path(const char *p, const struct linux_timespec *a,
                         const struct linux_timespec *m, int f) {
    (void)p;
    g_path_calls++; g_last_a = *a; g_last_m = *m; g_last_follow = f;
    return 0;
}
static int64_t fake_fd(int fd, const struct linux_timespec *a,
                       const struct linux_timespec *m) {
    g_fd_calls++; g_last_fd = fd; g_last_a = *a; g_last_m = *m;
    return 0;
}
static void fake_now(struct linux_timespec *out) {
    g_now_calls++;
    out->tv_sec = 1700000000; out->tv_nsec = 500000000;
}

static void install_fake(void) {
    static const struct linux_utime_ops o = {
        .utime_path = fake_path,
        .utime_fd   = fake_fd,
        .now        = fake_now,
    };
    g_path_calls = g_fd_calls = 0;
    g_now_calls = 0;
    g_last_a.tv_sec = g_last_a.tv_nsec = 0;
    g_last_m.tv_sec = g_last_m.tv_nsec = 0;
    g_last_follow = -1; g_last_fd = -1;
    linux_utime_reset_for_tests();
    linux_utime_install_ops(&o);
}

static void t1(void) {
    install_fake();
    TEST("utimensat(AT_FDCWD, NULL, ...) -> -EFAULT");
    if (linux_utimensat(LINUX_UTIME_AT_FDCWD, NULL, NULL, 0)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    install_fake();
    TEST("utimensat(7, NULL, NULL, 0) -> fd provider with NOW");
    int64_t r = linux_utimensat(7, NULL, NULL, 0);
    if (r == 0 && g_fd_calls == 1 && g_last_fd == 7 &&
        g_last_a.tv_sec == 1700000000 &&
        g_last_m.tv_sec == 1700000000) PASS();
    else FAIL("");
}
static void t3(void) {
    install_fake();
    TEST("utimensat(AT_FDCWD, \"\", ...) -> -ENOENT");
    if (linux_utimensat(LINUX_UTIME_AT_FDCWD, "", NULL, 0)
        == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t4(void) {
    install_fake();
    TEST("utimensat unknown flag -> -EINVAL");
    if (linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL, 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    TEST("utimensat with NOFOLLOW -> follow=0");
    int64_t r = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL,
                                LINUX_UTIME_AT_SYMLINK_NOFOLLOW);
    if (r == 0 && g_path_calls == 1 && g_last_follow == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    install_fake();
    struct linux_timespec ts[2] = {
        { .tv_sec = 0, .tv_nsec = LINUX_UTIME_OMIT },
        { .tv_sec = 0, .tv_nsec = LINUX_UTIME_OMIT },
    };
    int64_t r = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", ts, 0);
    TEST("utimensat with both UTIME_OMIT -> 0 (Linux fast path, no provider)");
    if (r == 0 && g_path_calls == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    struct linux_timespec ts[2] = {
        { .tv_sec = 100, .tv_nsec = 200 },
        { .tv_sec = 300, .tv_nsec = 400 },
    };
    int64_t r = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", ts, 0);
    TEST("utimensat with explicit timestamps forwards verbatim");
    if (r == 0 && g_path_calls == 1 &&
        g_last_a.tv_sec == 100 && g_last_a.tv_nsec == 200 &&
        g_last_m.tv_sec == 300 && g_last_m.tv_nsec == 400 &&
        g_last_follow == 1) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    struct linux_timespec ts[2] = {
        { .tv_sec = 0, .tv_nsec = 1000000000L },  /* invalid: ns >= 1e9 */
        { .tv_sec = 0, .tv_nsec = 0 },
    };
    TEST("utimensat with tv_nsec >= 1e9 -> -EINVAL");
    if (linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", ts, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    TEST("utimensat with negative dirfd != AT_FDCWD -> -EBADF");
    if (linux_utimensat(-50, NULL, NULL, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_utime_reset_for_tests();
    TEST("utimensat without ops -> -ENOSYS");
    if (linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL, 0)
        == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    TEST("utime(\"/x\", NULL) -> delegates to utimensat with NOW");
    int64_t r = linux_utime("/x", NULL);
    if (r == 0 && g_path_calls == 1) PASS();
    else FAIL("");
}
static void t12(void) {
    install_fake();
    TEST("utime(NULL, NULL) -> -EFAULT");
    if (linux_utime(NULL, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t13(void) {
    install_fake();
    int dummy = 0;
    TEST("utime(\"/x\", &buf) -> -ENOSYS (legacy buf decoded by provider)");
    if (linux_utime("/x", &dummy) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    TEST("futimesat(7,...) -> -ENOTDIR (only AT_FDCWD)");
    if (linux_futimesat(7, "/x", NULL) == -LINUX_ENOTDIR) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    int64_t r = linux_utimes("/x", NULL);
    TEST("utimes(\"/x\", NULL) -> delegates with NOW");
    if (r == 0 && g_path_calls == 1) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    int64_t r = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL, 0);
    TEST("utimensat NULL times -> NOW for both");
    if (r == 0 && g_path_calls == 1 &&
        g_last_a.tv_sec == 1700000000 &&
        g_last_m.tv_sec == 1700000000) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    /* Path AT non-AT_FDCWD is rejected even with valid path. */
    TEST("utimensat(7, \"/x\", ...) -> -ENOTDIR");
    if (linux_utimensat(7, "/x", NULL, 0) == -LINUX_ENOTDIR) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    linux_utime_install_ops(NULL);
    int64_t r1 = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL, 0);
    int64_t r2 = linux_utimensat(7, NULL, NULL, 0);
    TEST("utime install_ops(NULL) clears timestamp callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_path_calls == 0 && g_fd_calls == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}
static void t19(void) {
    install_fake();
    linux_utime_reset_for_tests();
    int64_t r1 = linux_utimensat(LINUX_UTIME_AT_FDCWD, "/x", NULL, 0);
    int64_t r2 = linux_utimensat(7, NULL, NULL, 0);
    TEST("utime reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_path_calls == 0 && g_fd_calls == 0 && g_now_calls == 0) PASS();
    else FAIL("");
}

int test_linux_utime_run(void) {
    printf("[test_linux_utime]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16();
    t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
