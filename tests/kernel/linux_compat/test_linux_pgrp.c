#include "kernel/linux_compat/linux_pgrp.h"
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

static int g_pid_calls;

static int32_t fake_pid(void) {
    g_pid_calls++;
    return 5;
}

static void install_pid(void) {
    static const struct linux_pgrp_ops o = { .getpid = fake_pid };
    linux_pgrp_reset_for_tests();
    g_pid_calls = 0;
    linux_pgrp_install_ops(&o);
}

/* --- getpgrp / getpgid --- */

static void t_getpgrp_default(void) {
    linux_pgrp_reset_for_tests();
    TEST("getpgrp() -> 1 (default Marco M1)");
    if (linux_getpgrp() == 1) PASS();
    else FAIL("default wrong");
}

static void t_getpgid_self_zero(void) {
    linux_pgrp_reset_for_tests();
    TEST("getpgid(0) -> 1 (self)");
    if (linux_getpgid(0) == 1) PASS();
    else FAIL("self failed");
}

static void t_getpgid_other_esrch(void) {
    install_pid();
    TEST("getpgid(42) -> -ESRCH (not self)");
    if (linux_getpgid(42) == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

/* --- setpgid --- */

static void t_setpgid_negative_pid_einval(void) {
    install_pid();
    TEST("setpgid(-1, 0) -> -EINVAL");
    if (linux_setpgid(-1, 0) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_setpgid_negative_pgid_einval(void) {
    install_pid();
    TEST("setpgid(0, -1) -> -EINVAL");
    if (linux_setpgid(0, -1) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_setpgid_other_target_eperm(void) {
    install_pid();
    TEST("setpgid(99, 99) -> -EPERM (not our child)");
    if (linux_setpgid(99, 99) == -LINUX_EPERM) PASS();
    else FAIL("EPERM not surfaced");
}

static void t_setpgid_self_zero_works(void) {
    install_pid();
    int64_t r = linux_setpgid(0, 0);
    TEST("setpgid(0, 0) -> 0 (self->self)");
    if (r == 0 && linux_getpgrp() == 5) PASS();
    else FAIL("self->self failed");
}

static void t_setpgid_self_to_42_works(void) {
    install_pid();
    int64_t r = linux_setpgid(5, 42);
    TEST("setpgid(self, 42) -> 0; getpgrp -> 42");
    if (r == 0 && linux_getpgrp() == 42) PASS();
    else FAIL("setpgid self->42 failed");
}

/* --- setsid / getsid --- */

static void t_setsid_first_call_succeeds(void) {
    install_pid();
    /* After install_pid, pgid = 1 (default), self = 5; we are
     * not yet a leader, so first setsid should succeed. */
    int64_t r = linux_setsid();
    TEST("setsid() -> self pid (5); first call");
    if (r == 5 && linux_getpgrp() == 5) PASS();
    else FAIL("first setsid wrong");
}

static void t_setsid_second_call_eperm(void) {
    install_pid();
    (void)linux_setsid();          /* become leader */
    int64_t r = linux_setsid();   /* second call: already leader */
    TEST("setsid() second call -> -EPERM (already leader)");
    if (r == -LINUX_EPERM) PASS();
    else FAIL("EPERM not surfaced");
}

static void t_getsid_self_zero(void) {
    install_pid();
    (void)linux_setsid();
    TEST("getsid(0) -> 5 (self)");
    if (linux_getsid(0) == 5) PASS();
    else FAIL("getsid self wrong");
}

static void t_getsid_other_esrch(void) {
    install_pid();
    TEST("getsid(99) -> -ESRCH");
    if (linux_getsid(99) == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

static void t_install_null_clears_getpid_callback(void) {
    install_pid();
    linux_pgrp_install_ops(NULL);
    int64_t r = linux_getpgid(0);
    TEST("pgrp install_ops(NULL) clears getpid callback");
    if (r == 1 && g_pid_calls == 0) PASS();
    else FAIL("stale getpid callback invoked");
}

static void t_reset_clears_getpid_callback(void) {
    install_pid();
    linux_pgrp_reset_for_tests();
    int64_t r = linux_setsid();
    TEST("pgrp reset clears installed callbacks");
    if (r == -LINUX_EPERM && g_pid_calls == 0) PASS();
    else FAIL("stale getpid callback invoked");
}

int test_linux_pgrp_run(void) {
    printf("[test_linux_pgrp]\n");
    tests_run = tests_passed = 0;

    t_getpgrp_default();
    t_getpgid_self_zero();
    t_getpgid_other_esrch();

    t_setpgid_negative_pid_einval();
    t_setpgid_negative_pgid_einval();
    t_setpgid_other_target_eperm();
    t_setpgid_self_zero_works();
    t_setpgid_self_to_42_works();

    t_setsid_first_call_succeeds();
    t_setsid_second_call_eperm();
    t_getsid_self_zero();
    t_getsid_other_esrch();
    t_install_null_clears_getpid_callback();
    t_reset_clears_getpid_callback();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
