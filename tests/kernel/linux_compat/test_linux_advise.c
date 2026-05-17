#include "kernel/linux_compat/linux_advise.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_send_calls;
static int g_last_in_fd;
static int g_last_out_fd;
static int64_t g_last_offset_in;

static int64_t fake_send(int out_fd, int in_fd,
                         int64_t *offset, size_t count) {
    g_send_calls++;
    g_last_out_fd = out_fd; g_last_in_fd = in_fd;
    if (offset) {
        g_last_offset_in = *offset;
        *offset += (int64_t)count;
    } else {
        g_last_offset_in = -1;
    }
    return (int64_t)count;
}

static void install_fake(void) {
    static const struct linux_advise_ops o = {
        .sendfile = fake_send,
    };
    g_send_calls = 0;
    g_last_in_fd = g_last_out_fd = -1;
    g_last_offset_in = 0;
    linux_advise_reset_for_tests();
    linux_advise_install_ops(&o);
}

static void t1(void) {
    TEST("posix_fadvise(-1, ...) -> -EBADF");
    if (linux_posix_fadvise(-1, 0, 0, LINUX_POSIX_FADV_NORMAL)
        == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t2(void) {
    TEST("posix_fadvise negative offset -> -EINVAL");
    if (linux_posix_fadvise(7, -1, 0, LINUX_POSIX_FADV_NORMAL)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    TEST("posix_fadvise negative len -> -EINVAL");
    if (linux_posix_fadvise(7, 0, -1, LINUX_POSIX_FADV_NORMAL)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("posix_fadvise unknown advice -> -EINVAL");
    if (linux_posix_fadvise(7, 0, 100, 99) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    TEST("posix_fadvise(POSIX_FADV_RANDOM) -> 0");
    if (linux_posix_fadvise(7, 0, 100, LINUX_POSIX_FADV_RANDOM) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    TEST("posix_fadvise(POSIX_FADV_NOREUSE) -> 0 (max advice)");
    if (linux_posix_fadvise(7, 0, 100, LINUX_POSIX_FADV_NOREUSE) == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    TEST("fallocate(-1, ...) -> -EBADF");
    if (linux_fallocate(-1, 0, 0, 100) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("fallocate len <= 0 -> -EINVAL");
    if (linux_fallocate(7, 0, 0, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    TEST("fallocate negative offset -> -EINVAL");
    if (linux_fallocate(7, 0, -1, 100) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    TEST("fallocate unknown mode bit -> -EINVAL");
    if (linux_fallocate(7, 0x1000, 0, 100) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("fallocate PUNCH_HOLE without KEEP_SIZE -> -EINVAL");
    if (linux_fallocate(7, LINUX_FALLOC_FL_PUNCH_HOLE, 0, 100)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("fallocate(PUNCH_HOLE|KEEP_SIZE) -> -EOPNOTSUPP (tmpfs)");
    if (linux_fallocate(7, LINUX_FALLOC_FL_PUNCH_HOLE |
                            LINUX_FALLOC_FL_KEEP_SIZE, 0, 100)
        == -LINUX_EOPNOTSUPP) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("fallocate basic -> -EOPNOTSUPP (tmpfs no preallocation)");
    if (linux_fallocate(7, 0, 0, 100) == -LINUX_EOPNOTSUPP) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    TEST("sendfile(-1, in, ...) -> -EBADF");
    if (linux_sendfile(-1, 7, NULL, 100) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    TEST("sendfile(out, -1, ...) -> -EBADF");
    if (linux_sendfile(7, -1, NULL, 100) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_advise_reset_for_tests();
    TEST("sendfile without ops -> -ENOSYS");
    if (linux_sendfile(7, 8, NULL, 100) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    int64_t off = 50;
    int64_t r = linux_sendfile(7, 8, &off, 100);
    TEST("sendfile delegates to provider and advances offset");
    if (r == 100 && g_send_calls == 1 && g_last_in_fd == 8 &&
        g_last_out_fd == 7 && g_last_offset_in == 50 &&
        off == 150) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    linux_advise_install_ops(NULL);
    int64_t off = 50;
    int64_t r = linux_sendfile(7, 8, &off, 100);
    TEST("advise install_ops(NULL) clears sendfile callback");
    if (r == -LINUX_ENOSYS && off == 50 && g_send_calls == 0) PASS();
    else FAIL("");
}
static void t19(void) {
    install_fake();
    linux_advise_reset_for_tests();
    int64_t off = 50;
    int64_t r = linux_sendfile(7, 8, &off, 100);
    TEST("advise reset clears installed callbacks");
    if (r == -LINUX_ENOSYS && off == 50 && g_send_calls == 0) PASS();
    else FAIL("");
}

int test_linux_advise_run(void) {
    printf("[test_linux_advise]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
