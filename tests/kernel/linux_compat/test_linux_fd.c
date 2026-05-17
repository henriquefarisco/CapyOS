/*
 * Host tests for linux_fd (S1.13 pipe + pipe2 + dup3).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_fd.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake fd ops. */
static int g_pipe_create_calls;
static int g_dup3_calls;
static int g_set_flags_calls;
static int g_dup3_return_value = 99;
static int g_pipe_create_return_value = 0;
static int g_last_fd_for_flags = -1;
static uint32_t g_last_flags_set = 0;
static int g_pipe_fds_to_emit[2] = {3, 4};

static int fake_pipe_create(int fds[2]) {
    g_pipe_create_calls++;
    if (g_pipe_create_return_value != 0) return g_pipe_create_return_value;
    fds[0] = g_pipe_fds_to_emit[0];
    fds[1] = g_pipe_fds_to_emit[1];
    return 0;
}

static int fake_dup3(int oldfd, int newfd) {
    (void)oldfd;
    g_dup3_calls++;
    if (g_dup3_return_value < 0) return -1;
    return newfd;  /* Linux dup3 always returns newfd on success. */
}

static void fake_set_flags(int fd, uint32_t flags) {
    g_set_flags_calls++;
    g_last_fd_for_flags = fd;
    g_last_flags_set = flags;
}

static void install_fake_ops(void) {
    linux_fd_reset_for_tests();
    g_pipe_create_calls = 0;
    g_dup3_calls = 0;
    g_set_flags_calls = 0;
    g_dup3_return_value = 99;
    g_pipe_create_return_value = 0;
    g_last_fd_for_flags = -1;
    g_last_flags_set = 0;
    g_pipe_fds_to_emit[0] = 3; g_pipe_fds_to_emit[1] = 4;

    static const struct linux_fd_ops ops = {
        .pipe_create  = fake_pipe_create,
        .dup3         = fake_dup3,
        .set_fd_flags = fake_set_flags,
    };
    linux_fd_install_ops(&ops);
}

/* -------- pipe / pipe2 -------- */

static void t_pipe_basic(void) {
    install_fake_ops();
    int fds[2] = {0};
    int64_t r = linux_pipe(fds);
    TEST("pipe: legacy wrapper returns 0, fills fds, applies zero flags");
    if (r == 0 && fds[0] == 3 && fds[1] == 4 &&
        g_pipe_create_calls == 1 && g_set_flags_calls == 2 &&
        g_last_flags_set == 0) PASS();
    else FAIL("legacy pipe wrapper wrong");
}

static void t_pipe_null_fds(void) {
    install_fake_ops();
    int64_t r = linux_pipe(NULL);
    TEST("pipe: NULL fds_out -> -EFAULT");
    if (r == -LINUX_EFAULT && g_pipe_create_calls == 0) PASS();
    else FAIL("legacy pipe NULL not rejected");
}

static void t_pipe_no_ops(void) {
    linux_fd_reset_for_tests();
    int fds[2];
    int64_t r = linux_pipe(fds);
    TEST("pipe: no ops installed -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("legacy pipe missing ENOSYS");
}

static void t_pipe2_basic(void) {
    install_fake_ops();
    int fds[2] = {0};
    int64_t r = linux_pipe2(fds, 0);
    TEST("pipe2: basic call returns 0, fills fds, calls set_flags 2x");
    if (r == 0 && fds[0] == 3 && fds[1] == 4 &&
        g_pipe_create_calls == 1 && g_set_flags_calls == 2) PASS();
    else FAIL("basic call wrong");
}

static void t_pipe2_null_fds(void) {
    install_fake_ops();
    int64_t r = linux_pipe2(NULL, 0);
    TEST("pipe2: NULL fds_out -> -EFAULT");
    if (r == -LINUX_EFAULT && g_pipe_create_calls == 0) PASS();
    else FAIL("null not rejected");
}

static void t_pipe2_unknown_flag(void) {
    install_fake_ops();
    int fds[2];
    int64_t r = linux_pipe2(fds, 0x10000000u);  /* outside known mask */
    TEST("pipe2: unknown flag bits -> -EINVAL (no pipe created)");
    if (r == -LINUX_EINVAL && g_pipe_create_calls == 0) PASS();
    else FAIL("flag mask not enforced");
}

static void t_pipe2_known_flags(void) {
    install_fake_ops();
    int fds[2];
    int64_t r1 = linux_pipe2(fds, LINUX_O_CLOEXEC);
    int r1_set = g_set_flags_calls;
    install_fake_ops();
    int64_t r2 = linux_pipe2(fds, LINUX_O_NONBLOCK);
    install_fake_ops();
    int64_t r3 = linux_pipe2(fds, LINUX_O_DIRECT);
    install_fake_ops();
    int64_t r4 = linux_pipe2(fds, LINUX_O_CLOEXEC|LINUX_O_NONBLOCK|LINUX_O_DIRECT);
    TEST("pipe2: O_CLOEXEC/O_NONBLOCK/O_DIRECT (and combos) accepted");
    if (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0 && r1_set == 2) PASS();
    else FAIL("known flag rejected");
}

static void t_pipe2_no_ops(void) {
    linux_fd_reset_for_tests();
    int fds[2];
    int64_t r = linux_pipe2(fds, 0);
    TEST("pipe2: no ops installed -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("missing ENOSYS");
}

static void t_pipe2_create_fails(void) {
    install_fake_ops();
    g_pipe_create_return_value = -1;
    int fds[2];
    int64_t r = linux_pipe2(fds, 0);
    TEST("pipe2: pipe_create -> -1 maps to -EMFILE");
    if (r == -LINUX_EMFILE) PASS(); else FAIL("expected -EMFILE");
}

static void t_pipe2_set_flags_propagated(void) {
    install_fake_ops();
    int fds[2];
    linux_pipe2(fds, LINUX_O_CLOEXEC | LINUX_O_NONBLOCK);
    TEST("pipe2: set_fd_flags receives the original flags mask");
    if (g_last_flags_set == (LINUX_O_CLOEXEC | LINUX_O_NONBLOCK)) PASS();
    else FAIL("flags not propagated");
}

static void t_install_null_clears_pipe_ops(void) {
    install_fake_ops();
    linux_fd_install_ops(NULL);
    int fds[2];
    int64_t r = linux_pipe2(fds, 0);
    TEST("linux_fd_install_ops(NULL) clears pipe callbacks");
    if (r == -LINUX_ENOSYS && g_pipe_create_calls == 0) PASS();
    else FAIL("pipe callbacks not cleared");
}

/* -------- dup3 -------- */

static void t_dup3_basic(void) {
    install_fake_ops();
    int64_t r = linux_dup3(3, 5, 0);
    TEST("dup3: basic call returns newfd, calls fake dup once");
    if (r == 5 && g_dup3_calls == 1) PASS();
    else FAIL("basic call wrong");
}

static void t_dup3_same_fd(void) {
    install_fake_ops();
    int64_t r = linux_dup3(3, 3, 0);
    TEST("dup3: oldfd == newfd -> -EINVAL (Linux semantics)");
    if (r == -LINUX_EINVAL && g_dup3_calls == 0) PASS();
    else FAIL("did not reject same fd");
}

static void t_dup3_unknown_flag(void) {
    install_fake_ops();
    /* O_NONBLOCK / O_DIRECT are NOT permitted by dup3 (only O_CLOEXEC). */
    int64_t r = linux_dup3(3, 5, LINUX_O_NONBLOCK);
    TEST("dup3: O_NONBLOCK rejected with -EINVAL (only O_CLOEXEC allowed)");
    if (r == -LINUX_EINVAL && g_dup3_calls == 0) PASS();
    else FAIL("flag not rejected");
}

static void t_dup3_negative_fd(void) {
    install_fake_ops();
    int64_t r1 = linux_dup3(-1, 5, 0);
    int64_t r2 = linux_dup3(3, -1, 0);
    TEST("dup3: negative fd -> -EBADF");
    if (r1 == -LINUX_EBADF && r2 == -LINUX_EBADF) PASS();
    else FAIL("negative not rejected");
}

static void t_dup3_no_ops(void) {
    linux_fd_reset_for_tests();
    int64_t r = linux_dup3(3, 5, 0);
    TEST("dup3: no ops installed -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("missing ENOSYS");
}

static void t_dup3_callback_fails(void) {
    install_fake_ops();
    g_dup3_return_value = -1;
    int64_t r = linux_dup3(3, 5, 0);
    TEST("dup3: callback -1 -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("expected -EBADF");
}

static void t_dup3_cloexec_propagated(void) {
    install_fake_ops();
    linux_dup3(3, 5, LINUX_O_CLOEXEC);
    TEST("dup3: O_CLOEXEC accepted and propagated to set_fd_flags");
    if (g_last_flags_set == LINUX_O_CLOEXEC && g_last_fd_for_flags == 5) PASS();
    else FAIL("CLOEXEC not propagated");
}

static void t_install_null_clears_dup3_ops(void) {
    install_fake_ops();
    linux_fd_install_ops(NULL);
    int64_t r = linux_dup3(3, 5, 0);
    TEST("linux_fd_install_ops(NULL) clears dup3 callbacks");
    if (r == -LINUX_ENOSYS && g_dup3_calls == 0) PASS();
    else FAIL("dup3 callbacks not cleared");
}

int test_linux_fd_run(void) {
    printf("[test_linux_fd]\n");
    tests_run = tests_passed = 0;

    t_pipe_basic();
    t_pipe_null_fds();
    t_pipe_no_ops();

    t_pipe2_basic();
    t_pipe2_null_fds();
    t_pipe2_unknown_flag();
    t_pipe2_known_flags();
    t_pipe2_no_ops();
    t_pipe2_create_fails();
    t_pipe2_set_flags_propagated();
    t_install_null_clears_pipe_ops();

    t_dup3_basic();
    t_dup3_same_fd();
    t_dup3_unknown_flag();
    t_dup3_negative_fd();
    t_dup3_no_ops();
    t_dup3_callback_fails();
    t_dup3_cloexec_propagated();
    t_install_null_clears_dup3_ops();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
