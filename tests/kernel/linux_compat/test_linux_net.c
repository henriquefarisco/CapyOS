/*
 * Host tests for linux_net (S1.14 accept4 + recvmmsg + sendmmsg).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_net.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake socket layer (would call into real socket layer in prod). */
static int g_accept4_calls;
static int g_recvmmsg_calls;
static int g_sendmmsg_calls;
static uint32_t g_last_flags;

static int fake_accept4(int s, void *a, uint32_t *al, uint32_t flags) {
    (void)s; (void)a; (void)al;
    g_accept4_calls++;
    g_last_flags = flags;
    return 5;  /* fake new fd */
}
static int fake_recvmmsg(int s, void *m, uint32_t v, uint32_t flags, void *t) {
    (void)s; (void)m; (void)v; (void)t;
    g_recvmmsg_calls++;
    g_last_flags = flags;
    return (int)v;
}
static int fake_sendmmsg(int s, void *m, uint32_t v, uint32_t flags) {
    (void)s; (void)m;
    g_sendmmsg_calls++;
    g_last_flags = flags;
    return (int)v;
}

static void install_fake(void) {
    linux_net_reset_for_tests();
    g_accept4_calls = g_recvmmsg_calls = g_sendmmsg_calls = 0;
    g_last_flags = 0;
    static const struct linux_net_ops ops = {
        .accept4  = fake_accept4,
        .recvmmsg = fake_recvmmsg,
        .sendmmsg = fake_sendmmsg,
    };
    linux_net_install_ops(&ops);
}

/* -------- accept4 -------- */

static void t_accept4_no_ops_enosys(void) {
    linux_net_reset_for_tests();
    int64_t r = linux_accept4(0, 0, 0, 0);
    TEST("accept4: no socket layer installed -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("expected ENOSYS");
}

static void t_accept4_negative_fd(void) {
    install_fake();
    int64_t r = linux_accept4(-1, 0, 0, 0);
    TEST("accept4: sockfd < 0 -> -EBADF (no callback)");
    if (r == -LINUX_EBADF && g_accept4_calls == 0) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_accept4_unknown_flag(void) {
    install_fake();
    int64_t r = linux_accept4(0, 0, 0, 0x1000000u);
    TEST("accept4: flag outside SOCK_NONBLOCK|SOCK_CLOEXEC -> -EINVAL");
    if (r == -LINUX_EINVAL && g_accept4_calls == 0) PASS();
    else FAIL("flag not validated");
}

static void t_accept4_known_flags_call_op(void) {
    install_fake();
    int64_t r = linux_accept4(3, 0x100, 0x200,
                              LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC);
    TEST("accept4: known flags -> calls op, returns its result");
    if (r == 5 && g_accept4_calls == 1 &&
        g_last_flags == (LINUX_SOCK_NONBLOCK|LINUX_SOCK_CLOEXEC)) PASS();
    else FAIL("op not called or flags wrong");
}

static void t_accept4_addr_without_addrlen(void) {
    install_fake();
    int64_t r = linux_accept4(3, 0x1000, 0, 0);
    TEST("accept4: addr_ptr non-null but addrlen_ptr NULL -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("addrlen requirement not enforced");
}

static void t_install_null_clears_accept4_ops(void) {
    install_fake();
    linux_net_install_ops(NULL);
    int64_t r = linux_accept4(3, 0, 0, 0);
    TEST("net install_ops(NULL) clears accept4 callback");
    if (r == -LINUX_ENOSYS && g_accept4_calls == 0) PASS();
    else FAIL("accept4 callback not cleared");
}

/* -------- recvmmsg -------- */

static void t_recvmmsg_no_ops_enosys(void) {
    linux_net_reset_for_tests();
    int64_t r = linux_recvmmsg(0, 0x100, 1, 0, 0);
    TEST("recvmmsg: no ops -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("expected ENOSYS");
}

static void t_recvmmsg_zero_vlen(void) {
    install_fake();
    int64_t r = linux_recvmmsg(0, 0, 0, 0, 0);
    TEST("recvmmsg: vlen=0 -> 0 (no op call)");
    if (r == 0 && g_recvmmsg_calls == 0) PASS();
    else FAIL("vlen=0 path wrong");
}

static void t_recvmmsg_negative_fd(void) {
    install_fake();
    int64_t r = linux_recvmmsg(-1, 0x100, 1, 0, 0);
    TEST("recvmmsg: sockfd < 0 -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_recvmmsg_null_msgvec(void) {
    install_fake();
    int64_t r = linux_recvmmsg(3, 0, 1, 0, 0);
    TEST("recvmmsg: NULL msgvec with vlen > 0 -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_recvmmsg_unknown_flag(void) {
    install_fake();
    int64_t r = linux_recvmmsg(3, 0x100, 1, 0xDEADBEEFu, 0);
    TEST("recvmmsg: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("flag not validated");
}

static void t_recvmmsg_clamps_vlen(void) {
    install_fake();
    /* vlen > MAX_VLEN (1024). Op should be called with the cap. */
    int64_t r = linux_recvmmsg(3, 0x100, LINUX_MMSG_MAX_VLEN + 100, 0, 0);
    TEST("recvmmsg: vlen > MAX_VLEN clamped to 1024");
    if (r == (int64_t)LINUX_MMSG_MAX_VLEN && g_recvmmsg_calls == 1) PASS();
    else FAIL("clamp wrong");
}

static void t_install_null_clears_recvmmsg_ops(void) {
    install_fake();
    linux_net_install_ops(NULL);
    int64_t r = linux_recvmmsg(3, 0x100, 1, 0, 0);
    TEST("net install_ops(NULL) clears recvmmsg callback");
    if (r == -LINUX_ENOSYS && g_recvmmsg_calls == 0) PASS();
    else FAIL("recvmmsg callback not cleared");
}

/* -------- sendmmsg -------- */

static void t_sendmmsg_basic(void) {
    install_fake();
    int64_t r = linux_sendmmsg(3, 0x200, 4, LINUX_MSG_DONTWAIT);
    TEST("sendmmsg: basic call returns op result, propagates flags");
    if (r == 4 && g_last_flags == LINUX_MSG_DONTWAIT) PASS();
    else FAIL("basic call wrong");
}

static void t_sendmmsg_zero_vlen(void) {
    install_fake();
    int64_t r = linux_sendmmsg(3, 0x100, 0, 0);
    TEST("sendmmsg: vlen=0 -> 0 (no op call)");
    if (r == 0 && g_sendmmsg_calls == 0) PASS();
    else FAIL("vlen=0 path wrong");
}

static void t_sendmmsg_negative_fd(void) {
    install_fake();
    int64_t r = linux_sendmmsg(-3, 0x100, 1, 0);
    TEST("sendmmsg: sockfd < 0 -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_install_null_clears_sendmmsg_ops(void) {
    install_fake();
    linux_net_install_ops(NULL);
    int64_t r = linux_sendmmsg(3, 0x200, 1, 0);
    TEST("net install_ops(NULL) clears sendmmsg callback");
    if (r == -LINUX_ENOSYS && g_sendmmsg_calls == 0) PASS();
    else FAIL("sendmmsg callback not cleared");
}

static void t_reset_clears_net_callbacks(void) {
    install_fake();
    linux_net_reset_for_tests();
    int64_t r1 = linux_accept4(3, 0, 0, 0);
    int64_t r2 = linux_sendmmsg(3, 0x200, 1, 0);
    TEST("net reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_accept4_calls == 0 && g_sendmmsg_calls == 0) PASS();
    else FAIL("reset callbacks not cleared");
}

int test_linux_net_run(void) {
    printf("[test_linux_net]\n");
    tests_run = tests_passed = 0;

    t_accept4_no_ops_enosys();
    t_accept4_negative_fd();
    t_accept4_unknown_flag();
    t_accept4_known_flags_call_op();
    t_accept4_addr_without_addrlen();
    t_install_null_clears_accept4_ops();

    t_recvmmsg_no_ops_enosys();
    t_recvmmsg_zero_vlen();
    t_recvmmsg_negative_fd();
    t_recvmmsg_null_msgvec();
    t_recvmmsg_unknown_flag();
    t_recvmmsg_clamps_vlen();
    t_install_null_clears_recvmmsg_ops();

    t_sendmmsg_basic();
    t_sendmmsg_zero_vlen();
    t_sendmmsg_negative_fd();
    t_install_null_clears_sendmmsg_ops();
    t_reset_clears_net_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
