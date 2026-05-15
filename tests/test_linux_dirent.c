#include "kernel/linux_compat/linux_dirent.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>
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

static void t_negative_fd_ebadf(void) {
    char buf[256];
    int64_t r = linux_getdents64(-1, buf, sizeof(buf));
    TEST("getdents64(-1, ...) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_zero_count_zero(void) {
    char buf[16];
    int64_t r = linux_getdents64(3, buf, 0);
    TEST("getdents64(fd, buf, 0) -> 0 (no entries returned)");
    if (r == 0) PASS();
    else FAIL("zero count path wrong");
}

static void t_null_buf_with_count_efault(void) {
    int64_t r = linux_getdents64(3, NULL, 64);
    TEST("getdents64(fd, NULL, 64) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_zero_eof_for_any_valid_fd(void) {
    char buf[256];
    int64_t r = linux_getdents64(7, buf, sizeof(buf));
    TEST("getdents64: returns 0 (EOF) for any valid fd");
    if (r == 0) PASS();
    else FAIL("EOF not surfaced");
}

static void t_high_fd_encoding_zero_eof(void) {
    char buf[256];
    /* Pseudo-fs encoded fd e.g. devfs 0x8005. */
    int64_t r = linux_getdents64(0x8005, buf, sizeof(buf));
    TEST("getdents64(devfs-encoded fd) returns 0 (EOF)");
    if (r == 0) PASS();
    else FAIL("EOF not surfaced for high fd");
}

static void t_int_min_fd_ebadf(void) {
    char buf[256];
    int64_t r = linux_getdents64(-2147483648, buf, sizeof(buf));
    TEST("getdents64(INT_MIN, ...) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced for INT_MIN");
}

int test_linux_dirent_run(void) {
    printf("[test_linux_dirent]\n");
    tests_run = tests_passed = 0;

    t_negative_fd_ebadf();
    t_zero_count_zero();
    t_null_buf_with_count_efault();
    t_zero_eof_for_any_valid_fd();
    t_high_fd_encoding_zero_eof();
    t_int_min_fd_ebadf();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
