#include "kernel/linux_compat/linux_pipe_zero.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_splice_calls;
static int g_tee_calls;
static int g_vmsplice_calls;
static int g_last_in_fd;
static int g_last_out_fd;

static int64_t fake_splice(int in, int64_t *oi, int out, int64_t *oo,
                           size_t len, uint32_t flags) {
    (void)oi; (void)oo; (void)flags;
    g_splice_calls++; g_last_in_fd = in; g_last_out_fd = out;
    return (int64_t)len;
}
static int64_t fake_tee(int in, int out, size_t len, uint32_t flags) {
    (void)flags;
    g_tee_calls++; g_last_in_fd = in; g_last_out_fd = out;
    return (int64_t)len;
}
static int64_t fake_vmsplice(int fd, const struct linux_pipe_iovec *iov,
                             size_t nr, uint32_t flags) {
    (void)iov; (void)flags;
    g_vmsplice_calls++; g_last_in_fd = fd;
    return (int64_t)nr;
}

static void install_fake(void) {
    static struct linux_pipe_zero_ops o;
    o.splice = fake_splice;
    o.tee = fake_tee;
    o.vmsplice = fake_vmsplice;
    g_splice_calls = g_tee_calls = g_vmsplice_calls = 0;
    g_last_in_fd = g_last_out_fd = -1;
    linux_pipe_zero_reset_for_tests();
    linux_pipe_zero_install_ops(&o);
}

static void t1(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("splice fd_in<0 -> -EBADF");
    if (linux_splice(-1, NULL, 7, NULL, 100, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("splice fd_out<0 -> -EBADF");
    if (linux_splice(7, NULL, -1, NULL, 100, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("splice unknown flags -> -EINVAL");
    if (linux_splice(7, NULL, 8, NULL, 100, 0xDEAD) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t4(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("splice without ops -> -ENOSYS");
    if (linux_splice(7, NULL, 8, NULL, 100, 0) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    int64_t r = linux_splice(7, NULL, 8, NULL, 100,
                             LINUX_SPLICE_F_MOVE);
    TEST("splice delegates to provider");
    if (r == 100 && g_splice_calls == 1 &&
        g_last_in_fd == 7 && g_last_out_fd == 8) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("tee fd_in<0 -> -EBADF");
    if (linux_tee(-1, 7, 100, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("tee unknown flags -> -EINVAL");
    if (linux_tee(7, 8, 100, 0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("tee without ops -> -ENOSYS");
    if (linux_tee(7, 8, 100, 0) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    int64_t r = linux_tee(7, 8, 100, 0);
    TEST("tee delegates to provider");
    if (r == 100 && g_tee_calls == 1) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("vmsplice fd<0 -> -EBADF");
    if (linux_vmsplice(-1, NULL, 0, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("vmsplice nr_segs > IOV_MAX -> -EINVAL");
    if (linux_vmsplice(7, NULL, LINUX_PIPE_ZERO_IOV_MAX + 1, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("vmsplice nr_segs=0 -> 0 (no-op)");
    if (linux_vmsplice(7, NULL, 0, 0) == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_pipe_zero_reset_for_tests();
    TEST("vmsplice nr_segs>0 NULL iov -> -EFAULT");
    if (linux_vmsplice(7, NULL, 5, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_pipe_zero_reset_for_tests();
    struct linux_pipe_iovec v = { .iov_base = "x", .iov_len = 1 };
    TEST("vmsplice without ops -> -ENOSYS");
    if (linux_vmsplice(7, &v, 1, 0) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    struct linux_pipe_iovec v = { .iov_base = "x", .iov_len = 1 };
    int64_t r = linux_vmsplice(7, &v, 1, 0);
    TEST("vmsplice delegates to provider");
    if (r == 1 && g_vmsplice_calls == 1) PASS();
    else FAIL("");
}
static void t16(void) {
    install_fake();
    linux_pipe_zero_install_ops(NULL);
    int64_t r = linux_splice(7, NULL, 8, NULL, 100, 0);
    TEST("pipe_zero install_ops(NULL) clears splice callback");
    if (r == -LINUX_ENOSYS && g_splice_calls == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    linux_pipe_zero_install_ops(NULL);
    int64_t r = linux_tee(7, 8, 100, 0);
    TEST("pipe_zero install_ops(NULL) clears tee callback");
    if (r == -LINUX_ENOSYS && g_tee_calls == 0) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    linux_pipe_zero_install_ops(NULL);
    struct linux_pipe_iovec v = { .iov_base = "x", .iov_len = 1 };
    int64_t r = linux_vmsplice(7, &v, 1, 0);
    TEST("pipe_zero install_ops(NULL) clears vmsplice callback");
    if (r == -LINUX_ENOSYS && g_vmsplice_calls == 0) PASS();
    else FAIL("");
}
static void t19(void) {
    install_fake();
    linux_pipe_zero_reset_for_tests();
    struct linux_pipe_iovec v = { .iov_base = "x", .iov_len = 1 };
    int64_t r1 = linux_splice(7, NULL, 8, NULL, 100, 0);
    int64_t r2 = linux_vmsplice(7, &v, 1, 0);
    TEST("pipe_zero reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_splice_calls == 0 && g_vmsplice_calls == 0) PASS();
    else FAIL("");
}

int test_linux_pipe_zero_run(void) {
    printf("[test_linux_pipe_zero]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14(); t15();
    t16(); t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
