#include "kernel/linux_compat/linux_ioctl.h"
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

static void t_negative_fd_ebadf(void) {
    int64_t r = linux_ioctl(-1, LINUX_TCGETS, 0);
    TEST("ioctl(-1, ...) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_tcgets_returns_enotty(void) {
    /* THE musl stdio init pattern: ioctl(stdin, TCGETS, &t).
     * Linux returns ENOTTY for non-tty fds; we match. */
    int64_t r = linux_ioctl(0, LINUX_TCGETS, 0xDEADBEEF);
    TEST("ioctl(stdin=0, TCGETS) -> -ENOTTY (musl stdio init key)");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("ENOTTY not surfaced");
}

static void t_tcsets_returns_enotty(void) {
    int64_t r = linux_ioctl(1, LINUX_TCSETS, 0);
    TEST("ioctl(stdout=1, TCSETS) -> -ENOTTY");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("ENOTTY not surfaced");
}

static void t_tiocgwinsz_returns_enotty(void) {
    int64_t r = linux_ioctl(2, LINUX_TIOCGWINSZ, 0);
    TEST("ioctl(stderr=2, TIOCGWINSZ) -> -ENOTTY");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("ENOTTY not surfaced");
}

static void t_unknown_cmd_still_enotty(void) {
    /* Unknown commands also return ENOTTY (Linux behaviour: the
     * driver dispatches first, only after the driver decides
     * does it return EINVAL or its own errno). */
    int64_t r = linux_ioctl(0, 0xABCDEF12, 0);
    TEST("ioctl with unknown cmd on non-tty fd -> -ENOTTY");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("ENOTTY not surfaced");
}

static void t_high_fd_encoding_handled(void) {
    /* Pseudo-fs fds are encoded with high bits (e.g. devfs
     * 0x8000+slot). Make sure the fd >= 0 check accepts them. */
    int64_t r = linux_ioctl(0x8005, LINUX_FIONREAD, 0);
    TEST("ioctl(devfs-encoded fd, FIONREAD) -> -ENOTTY");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("ENOTTY not surfaced for high fd");
}

static void t_zero_arg_accepted(void) {
    int64_t r = linux_ioctl(0, LINUX_TCGETS, 0);
    TEST("ioctl: zero arg accepted (returns ENOTTY same as non-zero)");
    if (r == -LINUX_ENOTTY) PASS();
    else FAIL("zero arg path wrong");
}

static void t_int_min_fd_ebadf(void) {
    int64_t r = linux_ioctl(-2147483648, LINUX_TCGETS, 0);
    TEST("ioctl(INT_MIN) -> -EBADF (negative fd validated)");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced for INT_MIN");
}

int test_linux_ioctl_run(void) {
    printf("[test_linux_ioctl]\n");
    tests_run = tests_passed = 0;

    t_negative_fd_ebadf();
    t_tcgets_returns_enotty();
    t_tcsets_returns_enotty();
    t_tiocgwinsz_returns_enotty();
    t_unknown_cmd_still_enotty();
    t_high_fd_encoding_handled();
    t_zero_arg_accepted();
    t_int_min_fd_ebadf();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
