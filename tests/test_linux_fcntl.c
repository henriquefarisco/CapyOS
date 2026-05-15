#include "kernel/linux_compat/linux_fcntl.h"
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
    linux_fcntl_reset_for_tests();
    int64_t r = linux_fcntl(-1, LINUX_F_GETFD, 0);
    TEST("fcntl(-1, F_GETFD) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_getfd_default_zero(void) {
    linux_fcntl_reset_for_tests();
    int64_t r = linux_fcntl(3, LINUX_F_GETFD, 0);
    TEST("fcntl(3, F_GETFD) initially returns 0 (FD_CLOEXEC clear)");
    if (r == 0) PASS();
    else FAIL("default not zero");
}

static void t_setfd_then_getfd_round_trip(void) {
    linux_fcntl_reset_for_tests();
    int64_t s = linux_fcntl(3, LINUX_F_SETFD, LINUX_FD_CLOEXEC);
    int64_t g = linux_fcntl(3, LINUX_F_GETFD, 0);
    TEST("fcntl: F_SETFD(FD_CLOEXEC) then F_GETFD round-trips");
    if (s == 0 && g == LINUX_FD_CLOEXEC) PASS();
    else FAIL("round trip failed");
}

static void t_setfd_drops_unknown_bits(void) {
    linux_fcntl_reset_for_tests();
    /* arg has FD_CLOEXEC | garbage; only FD_CLOEXEC should
     * survive. */
    (void)linux_fcntl(5, LINUX_F_SETFD, 0xFFFFFFFF);
    int64_t g = linux_fcntl(5, LINUX_F_GETFD, 0);
    TEST("fcntl F_SETFD: unknown bits silently masked off");
    if (g == LINUX_FD_CLOEXEC) PASS();
    else FAIL("unknown bits leaked through");
}

static void t_getfl_includes_rdwr_default(void) {
    linux_fcntl_reset_for_tests();
    int64_t r = linux_fcntl(7, LINUX_F_GETFL, 0);
    TEST("fcntl F_GETFL initial: includes O_RDWR access mode (=2)");
    /* O_ACCMODE = 0x3; the access mode byte should be 2 (O_RDWR). */
    if ((r & 0x3) == 0x2) PASS();
    else FAIL("access mode wrong");
}

static void t_setfl_then_getfl_round_trip(void) {
    linux_fcntl_reset_for_tests();
    (void)linux_fcntl(7, LINUX_F_SETFL,
                      LINUX_FCNTL_O_NONBLOCK | LINUX_FCNTL_O_APPEND);
    int64_t r = linux_fcntl(7, LINUX_F_GETFL, 0);
    TEST("fcntl: F_SETFL then F_GETFL round-trips O_NONBLOCK|O_APPEND");
    if ((r & LINUX_FCNTL_O_NONBLOCK) &&
        (r & LINUX_FCNTL_O_APPEND) &&
        (r & 0x3) == 0x2) PASS();
    else FAIL("flags or accmode lost");
}

static void t_setfl_drops_immutable_bits(void) {
    linux_fcntl_reset_for_tests();
    /* F_SETFL ignores access mode, O_CREAT/EXCL/TRUNC, etc.
     * Only the SETFL_MASK subset should be retained. */
    (void)linux_fcntl(9, LINUX_F_SETFL,
                      0xFFFFFFFF /* everything */ );
    int64_t r = linux_fcntl(9, LINUX_F_GETFL, 0);
    /* The retained bits should be exactly the SETFL_MASK plus
     * the access-mode default (O_RDWR). */
    int64_t expected = LINUX_FCNTL_SETFL_MASK | 0x2;
    TEST("fcntl F_SETFL: immutable bits silently dropped");
    if (r == expected) PASS();
    else FAIL("kept immutable bits or lost mutable ones");
}

static void t_dupfd_returns_enosys(void) {
    int64_t r = linux_fcntl(3, LINUX_F_DUPFD, 10);
    TEST("fcntl F_DUPFD -> -ENOSYS (need real fd table)");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_dupfd_cloexec_returns_enosys(void) {
    int64_t r = linux_fcntl(3, LINUX_F_DUPFD_CLOEXEC, 10);
    TEST("fcntl F_DUPFD_CLOEXEC -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_lock_commands_return_enosys(void) {
    int64_t a = linux_fcntl(3, LINUX_F_GETLK, 0);
    int64_t b = linux_fcntl(3, LINUX_F_SETLK, 0);
    int64_t c = linux_fcntl(3, LINUX_F_SETLKW, 0);
    TEST("fcntl F_GETLK/SETLK/SETLKW -> -ENOSYS");
    if (a == -LINUX_ENOSYS && b == -LINUX_ENOSYS && c == -LINUX_ENOSYS)
        PASS();
    else FAIL("at least one lock cmd not ENOSYS");
}

static void t_unknown_cmd_einval(void) {
    int64_t r = linux_fcntl(3, 0xDEAD, 0);
    TEST("fcntl unknown cmd -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_per_fd_independence(void) {
    linux_fcntl_reset_for_tests();
    /* fd 3 gets CLOEXEC, fd 4 stays clear. */
    (void)linux_fcntl(3, LINUX_F_SETFD, LINUX_FD_CLOEXEC);
    int64_t r3 = linux_fcntl(3, LINUX_F_GETFD, 0);
    int64_t r4 = linux_fcntl(4, LINUX_F_GETFD, 0);
    TEST("fcntl: per-fd flags are independent");
    if (r3 == LINUX_FD_CLOEXEC && r4 == 0) PASS();
    else FAIL("flags bled across fds");
}

static void t_high_fd_encoding_works(void) {
    linux_fcntl_reset_for_tests();
    /* Pseudo-fs fds (e.g. devfs 0x8005) must work for SETFD/GETFD. */
    (void)linux_fcntl(0x8005, LINUX_F_SETFD, LINUX_FD_CLOEXEC);
    int64_t r = linux_fcntl(0x8005, LINUX_F_GETFD, 0);
    TEST("fcntl: high-encoded fds (e.g. 0x8005 devfs) are tracked");
    if (r == LINUX_FD_CLOEXEC) PASS();
    else FAIL("high-encoded fd flags not tracked");
}

int test_linux_fcntl_run(void) {
    printf("[test_linux_fcntl]\n");
    tests_run = tests_passed = 0;

    t_negative_fd_ebadf();
    t_getfd_default_zero();
    t_setfd_then_getfd_round_trip();
    t_setfd_drops_unknown_bits();
    t_getfl_includes_rdwr_default();
    t_setfl_then_getfl_round_trip();
    t_setfl_drops_immutable_bits();
    t_dupfd_returns_enosys();
    t_dupfd_cloexec_returns_enosys();
    t_lock_commands_return_enosys();
    t_unknown_cmd_einval();
    t_per_fd_independence();
    t_high_fd_encoding_works();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
