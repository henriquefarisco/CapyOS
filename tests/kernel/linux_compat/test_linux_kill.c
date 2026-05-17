#include "kernel/linux_compat/linux_kill.h"
#include "kernel/linux_compat/linux_signal.h"
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

/* --- ops fixture --- */

static int g_deliver_calls = 0;
static int32_t g_last_pid  = -1;
static int     g_last_sig  = -1;
static int32_t g_fake_pid  = 7;

static int32_t fake_getpid(void) { return g_fake_pid; }
static int64_t fake_deliver(int32_t pid, int sig) {
    g_deliver_calls++;
    g_last_pid = pid;
    g_last_sig = sig;
    return 0;
}

static void install_fake(void) {
    static const struct linux_kill_ops ops = {
        .getpid = fake_getpid,
        .deliver = fake_deliver,
    };
    g_deliver_calls = 0;
    g_last_pid = g_last_sig = -1;
    linux_kill_reset_for_tests();
    linux_kill_install_ops(&ops);
}

/* --- kill --- */

static void t_kill_invalid_sig_einval(void) {
    install_fake();
    TEST("kill(self, -1) -> -EINVAL");
    if (linux_kill(7, -1) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_kill_huge_sig_einval(void) {
    install_fake();
    TEST("kill(self, 100) -> -EINVAL (sig > NSIG)");
    if (linux_kill(7, 100) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_kill_self_sig_zero_alive(void) {
    install_fake();
    TEST("kill(self, 0) -> 0 (alive probe)");
    if (linux_kill(7, 0) == 0 && g_deliver_calls == 0) PASS();
    else FAIL("alive probe wrong");
}

static void t_kill_self_real_sig_delivered(void) {
    install_fake();
    int64_t r = linux_kill(7, LINUX_SIGTERM);
    TEST("kill(self, SIGTERM) -> 0 + ops.deliver invoked");
    if (r == 0 && g_deliver_calls == 1 &&
        g_last_pid == 7 && g_last_sig == LINUX_SIGTERM) PASS();
    else FAIL("ops.deliver not invoked");
}

static void t_kill_self_no_ops_no_op(void) {
    linux_kill_reset_for_tests();
    TEST("kill(self, SIGTERM) without ops -> 0 (no-op success)");
    if (linux_kill(1, LINUX_SIGTERM) == 0) PASS();
    else FAIL("no-op success not returned");
}

static void t_kill_pid_zero_pgrp(void) {
    install_fake();
    TEST("kill(0, SIGTERM) -> 0 (own pgrp, no peers)");
    if (linux_kill(0, LINUX_SIGTERM) == 0 && g_deliver_calls == 0) PASS();
    else FAIL("pgrp 0 wrong");
}

static void t_kill_pid_minus_one_broadcast(void) {
    install_fake();
    TEST("kill(-1, SIGTERM) -> 0 (broadcast, no peers)");
    if (linux_kill(-1, LINUX_SIGTERM) == 0 && g_deliver_calls == 0) PASS();
    else FAIL("broadcast wrong");
}

static void t_kill_pid_other_esrch(void) {
    install_fake();
    TEST("kill(42, SIGTERM) -> -ESRCH (no such process)");
    if (linux_kill(42, LINUX_SIGTERM) == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

static void t_kill_pid_neg_pgrp_esrch(void) {
    install_fake();
    TEST("kill(-42, SIGTERM) -> -ESRCH (no such pgrp)");
    if (linux_kill(-42, LINUX_SIGTERM) == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

/* --- tgkill --- */

static void t_tgkill_self_real_sig(void) {
    install_fake();
    int64_t r = linux_tgkill(7, 7, LINUX_SIGUSR1);
    TEST("tgkill(self, self, SIGUSR1) -> 0 + delivered");
    if (r == 0 && g_deliver_calls == 1) PASS();
    else FAIL("ops not invoked");
}

static void t_tgkill_other_esrch(void) {
    install_fake();
    TEST("tgkill(42, 42, SIGUSR1) -> -ESRCH");
    if (linux_tgkill(42, 42, LINUX_SIGUSR1) == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

static void t_tgkill_zero_tgid_einval(void) {
    install_fake();
    TEST("tgkill(0, 7, SIGTERM) -> -EINVAL");
    if (linux_tgkill(0, 7, LINUX_SIGTERM) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

/* --- tkill --- */

static void t_tkill_self_real_sig(void) {
    install_fake();
    int64_t r = linux_tkill(7, LINUX_SIGUSR2);
    TEST("tkill(self, SIGUSR2) -> 0 + delivered");
    if (r == 0 && g_deliver_calls == 1) PASS();
    else FAIL("ops not invoked");
}

static void t_tkill_zero_tid_einval(void) {
    install_fake();
    TEST("tkill(0, SIGTERM) -> -EINVAL");
    if (linux_tkill(0, LINUX_SIGTERM) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_install_null_clears_signal_callbacks(void) {
    install_fake();
    linux_kill_install_ops(NULL);
    int64_t r1 = linux_kill(1, LINUX_SIGTERM);
    int64_t r2 = linux_tgkill(1, 1, LINUX_SIGUSR1);
    int64_t r3 = linux_tkill(1, LINUX_SIGUSR2);
    TEST("kill install_ops(NULL) clears signal callbacks");
    if (r1 == 0 && r2 == 0 && r3 == 0 && g_deliver_calls == 0) PASS();
    else FAIL("stale deliver invoked");
}

static void t_reset_clears_signal_callbacks(void) {
    install_fake();
    linux_kill_reset_for_tests();
    int64_t r1 = linux_kill(1, LINUX_SIGTERM);
    int64_t r2 = linux_tgkill(1, 1, LINUX_SIGUSR1);
    int64_t r3 = linux_tkill(1, LINUX_SIGUSR2);
    TEST("kill reset clears installed callbacks");
    if (r1 == 0 && r2 == 0 && r3 == 0 && g_deliver_calls == 0) PASS();
    else FAIL("stale deliver invoked");
}

int test_linux_kill_run(void) {
    printf("[test_linux_kill]\n");
    tests_run = tests_passed = 0;

    t_kill_invalid_sig_einval();
    t_kill_huge_sig_einval();
    t_kill_self_sig_zero_alive();
    t_kill_self_real_sig_delivered();
    t_kill_self_no_ops_no_op();
    t_kill_pid_zero_pgrp();
    t_kill_pid_minus_one_broadcast();
    t_kill_pid_other_esrch();
    t_kill_pid_neg_pgrp_esrch();

    t_tgkill_self_real_sig();
    t_tgkill_other_esrch();
    t_tgkill_zero_tgid_einval();

    t_tkill_self_real_sig();
    t_tkill_zero_tid_einval();
    t_install_null_clears_signal_callbacks();
    t_reset_clears_signal_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
