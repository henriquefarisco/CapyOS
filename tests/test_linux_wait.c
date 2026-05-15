#include "kernel/linux_compat/linux_wait.h"
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

/* --- wait4 --- */

static void t_wait4_no_children_echild(void) {
    int status = 0xdead;
    TEST("wait4(-1, &status, 0, NULL) -> -ECHILD (no children)");
    if (linux_wait4(-1, &status, 0, NULL) == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

static void t_wait4_unknown_options_einval(void) {
    TEST("wait4(-1, NULL, 0xDEAD, NULL) -> -EINVAL");
    if (linux_wait4(-1, NULL, 0xDEAD, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_wait4_wnohang_no_children_echild(void) {
    TEST("wait4(-1, NULL, WNOHANG, NULL) -> -ECHILD");
    if (linux_wait4(-1, NULL, LINUX_WNOHANG, NULL) == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

static void t_wait4_specific_pid_echild(void) {
    TEST("wait4(42, NULL, 0, NULL) -> -ECHILD (no such child)");
    if (linux_wait4(42, NULL, 0, NULL) == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

/* --- waitid --- */

static void t_waitid_invalid_idtype_einval(void) {
    TEST("waitid(99, 0, NULL, WEXITED) -> -EINVAL");
    if (linux_waitid(99, 0, NULL, LINUX_WEXITED) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_waitid_no_event_options_einval(void) {
    TEST("waitid(P_ALL, 0, NULL, 0) -> -EINVAL (no event flag)");
    if (linux_waitid(LINUX_P_ALL, 0, NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_waitid_unknown_options_einval(void) {
    TEST("waitid(P_ALL, 0, NULL, 0xDEAD) -> -EINVAL");
    if (linux_waitid(LINUX_P_ALL, 0, NULL, 0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_waitid_p_all_wexited_echild(void) {
    TEST("waitid(P_ALL, 0, NULL, WEXITED) -> -ECHILD");
    if (linux_waitid(LINUX_P_ALL, 0, NULL, LINUX_WEXITED)
        == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

static void t_waitid_p_pid_wexited_echild(void) {
    TEST("waitid(P_PID, 42, NULL, WEXITED) -> -ECHILD");
    if (linux_waitid(LINUX_P_PID, 42, NULL, LINUX_WEXITED)
        == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

static void t_waitid_p_pidfd_wexited_echild(void) {
    TEST("waitid(P_PIDFD, 7, NULL, WEXITED|WNOHANG) -> -ECHILD");
    if (linux_waitid(LINUX_P_PIDFD, 7, NULL,
                     LINUX_WEXITED | LINUX_WNOHANG) == -LINUX_ECHILD) PASS();
    else FAIL("ECHILD not surfaced");
}

int test_linux_wait_run(void) {
    printf("[test_linux_wait]\n");
    tests_run = tests_passed = 0;

    t_wait4_no_children_echild();
    t_wait4_unknown_options_einval();
    t_wait4_wnohang_no_children_echild();
    t_wait4_specific_pid_echild();

    t_waitid_invalid_idtype_einval();
    t_waitid_no_event_options_einval();
    t_waitid_unknown_options_einval();
    t_waitid_p_all_wexited_echild();
    t_waitid_p_pid_wexited_echild();
    t_waitid_p_pidfd_wexited_echild();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
