/* Host tests for linux_signal (S1.12). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_signal.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* -------- rt_sigaction -------- */

static void t_sigaction_basic(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction act = {
        .sa_handler = 0xDEADBEEFull,
        .sa_flags   = LINUX_SA_RESTART,
        .sa_mask    = 0xFFull,
    };
    int64_t r = linux_rt_sigaction(LINUX_SIGUSR1, &act, NULL, 8);
    const struct linux_sigaction *got = linux_signal_test_get_action(LINUX_SIGUSR1);
    TEST("rt_sigaction: stores handler/flags/mask for SIGUSR1");
    if (r == 0 && got && got->sa_handler == 0xDEADBEEFull &&
        got->sa_flags == LINUX_SA_RESTART && got->sa_mask == 0xFFull) PASS();
    else FAIL("storage wrong");
}

static void t_sigaction_oact_returns_previous(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction first = { .sa_handler = 0x1111ull };
    linux_rt_sigaction(LINUX_SIGTERM, &first, NULL, 8);

    struct linux_sigaction next = { .sa_handler = 0x2222ull };
    struct linux_sigaction prev = {0};
    int64_t r = linux_rt_sigaction(LINUX_SIGTERM, &next, &prev, 8);
    TEST("rt_sigaction: oact returns previous handler");
    if (r == 0 && prev.sa_handler == 0x1111ull) PASS();
    else FAIL("oact wrong");
}

static void t_sigaction_kill_einval(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction act = { .sa_handler = 0x1ull };
    int64_t r1 = linux_rt_sigaction(LINUX_SIGKILL, &act, NULL, 8);
    int64_t r2 = linux_rt_sigaction(LINUX_SIGSTOP, &act, NULL, 8);
    TEST("rt_sigaction: SIGKILL/SIGSTOP cannot be caught -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL) PASS();
    else FAIL("uncatchable not rejected");
}

static void t_sigaction_bad_signum(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction act = { .sa_handler = 0x1ull };
    int64_t r1 = linux_rt_sigaction(0, &act, NULL, 8);
    int64_t r2 = linux_rt_sigaction(65, &act, NULL, 8);
    TEST("rt_sigaction: signum out of [1, NSIG] -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL) PASS();
    else FAIL("bad signum accepted");
}

static void t_sigaction_bad_sigsetsize(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction act = { .sa_handler = 0x1ull };
    int64_t r = linux_rt_sigaction(LINUX_SIGINT, &act, NULL, 16);
    TEST("rt_sigaction: sigsetsize != 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("sigsetsize not validated");
}

static void t_sigaction_unknown_flag(void) {
    linux_signal_reset_for_tests();
    struct linux_sigaction act = {
        .sa_handler = 0x1ull,
        .sa_flags   = 0x100ull,  /* outside known mask */
    };
    int64_t r = linux_rt_sigaction(LINUX_SIGUSR1, &act, NULL, 8);
    TEST("rt_sigaction: unknown sa_flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("flag mask not enforced");
}

/* -------- rt_sigprocmask -------- */

static void t_procmask_block_unblock_setmask(void) {
    linux_signal_reset_for_tests();
    uint64_t set = 0x55ull;
    int64_t r1 = linux_rt_sigprocmask(LINUX_SIG_BLOCK,
                                      (uint64_t)(uintptr_t)&set, 0, 8);
    uint64_t after_block = linux_signal_test_get_mask();

    uint64_t unblock = 0x05ull;
    int64_t r2 = linux_rt_sigprocmask(LINUX_SIG_UNBLOCK,
                                      (uint64_t)(uintptr_t)&unblock, 0, 8);
    uint64_t after_unblock = linux_signal_test_get_mask();

    uint64_t setmask = 0xFFull;
    int64_t r3 = linux_rt_sigprocmask(LINUX_SIG_SETMASK,
                                      (uint64_t)(uintptr_t)&setmask, 0, 8);
    uint64_t after_setmask = linux_signal_test_get_mask();

    TEST("rt_sigprocmask: BLOCK/UNBLOCK/SETMASK update mask correctly");
    /* after BLOCK: 0x55. after UNBLOCK 0x05: 0x50. after SETMASK 0xFF: 0xFF. */
    if (r1 == 0 && r2 == 0 && r3 == 0 &&
        after_block == 0x55 && after_unblock == 0x50 &&
        after_setmask == 0xFFull) PASS();
    else FAIL("mask transitions wrong");
}

static void t_procmask_kill_stop_unmaskable(void) {
    linux_signal_reset_for_tests();
    /* Try to mask SIGKILL (bit 8) and SIGSTOP (bit 18). They must
     * never make it into the mask. */
    uint64_t set = ((uint64_t)1 << (LINUX_SIGKILL - 1)) |
                   ((uint64_t)1 << (LINUX_SIGSTOP - 1)) |
                   ((uint64_t)1 << (LINUX_SIGINT  - 1));
    linux_rt_sigprocmask(LINUX_SIG_SETMASK,
                         (uint64_t)(uintptr_t)&set, 0, 8);
    uint64_t got = linux_signal_test_get_mask();
    TEST("rt_sigprocmask: SIGKILL/SIGSTOP forced clear (only SIGINT remains)");
    if (got == ((uint64_t)1 << (LINUX_SIGINT - 1))) PASS();
    else FAIL("uncatchable bits leaked");
}

static void t_procmask_oldset(void) {
    linux_signal_reset_for_tests();
    uint64_t initial = 0x33ull;
    linux_rt_sigprocmask(LINUX_SIG_SETMASK,
                         (uint64_t)(uintptr_t)&initial, 0, 8);

    uint64_t old = 0;
    int64_t r = linux_rt_sigprocmask(0, 0, (uint64_t)(uintptr_t)&old, 8);
    TEST("rt_sigprocmask: set==NULL fills oldset (query mode)");
    if (r == 0 && old == 0x33ull) PASS();
    else FAIL("oldset not filled");
}

static void t_procmask_bad_how(void) {
    linux_signal_reset_for_tests();
    uint64_t set = 0;
    int64_t r = linux_rt_sigprocmask(99, (uint64_t)(uintptr_t)&set, 0, 8);
    TEST("rt_sigprocmask: unknown how -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("bad how accepted");
}

static void t_procmask_bad_sigsetsize(void) {
    linux_signal_reset_for_tests();
    uint64_t set = 0;
    int64_t r = linux_rt_sigprocmask(LINUX_SIG_BLOCK,
                                     (uint64_t)(uintptr_t)&set, 0, 4);
    TEST("rt_sigprocmask: sigsetsize != 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("sigsetsize not validated");
}

/* -------- rt_sigreturn -------- */

static void t_sigreturn_enosys(void) {
    int64_t r = linux_rt_sigreturn();
    TEST("rt_sigreturn: -ENOSYS until signal delivery infra lands");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("expected ENOSYS");
}

/* -------- sigaltstack -------- */

static void t_sigaltstack_basic(void) {
    linux_signal_reset_for_tests();
    struct linux_stack_t ss = {
        .ss_sp = 0x10000ull, .ss_flags = 0, .ss_size = 8192,
    };
    int64_t r = linux_sigaltstack(&ss, NULL);
    const struct linux_stack_t *got = linux_signal_test_get_altstack();
    TEST("sigaltstack: stores ss_sp/size/flags");
    if (r == 0 && got->ss_sp == 0x10000ull &&
        got->ss_size == 8192 && got->ss_flags == 0) PASS();
    else FAIL("storage wrong");
}

static void t_sigaltstack_too_small(void) {
    linux_signal_reset_for_tests();
    struct linux_stack_t ss = { .ss_sp = 0x1000ull, .ss_size = 100 };
    int64_t r = linux_sigaltstack(&ss, NULL);
    TEST("sigaltstack: ss_size < MINSIGSTKSZ (2048) -> -ENOMEM");
    if (r == -LINUX_ENOMEM) PASS();
    else FAIL("size not validated");
}

static void t_sigaltstack_disable_skips_size(void) {
    linux_signal_reset_for_tests();
    /* ss_flags=DISABLE allows tiny size (Linux skips the size check). */
    struct linux_stack_t ss = { .ss_size = 100, .ss_flags = LINUX_SS_DISABLE };
    int64_t r = linux_sigaltstack(&ss, NULL);
    TEST("sigaltstack: SS_DISABLE bypasses size check");
    if (r == 0) PASS();
    else FAIL("DISABLE rejected");
}

static void t_sigaltstack_old_ss(void) {
    linux_signal_reset_for_tests();
    struct linux_stack_t first = { .ss_sp = 0x9000ull, .ss_size = 4096 };
    linux_sigaltstack(&first, NULL);

    struct linux_stack_t old = {0};
    struct linux_stack_t next = { .ss_sp = 0x8000ull, .ss_size = 8192 };
    int64_t r = linux_sigaltstack(&next, &old);
    TEST("sigaltstack: old_ss returns previous stack");
    if (r == 0 && old.ss_sp == 0x9000ull) PASS();
    else FAIL("old_ss wrong");
}

int test_linux_signal_run(void) {
    printf("[test_linux_signal]\n");
    tests_run = tests_passed = 0;

    t_sigaction_basic();
    t_sigaction_oact_returns_previous();
    t_sigaction_kill_einval();
    t_sigaction_bad_signum();
    t_sigaction_bad_sigsetsize();
    t_sigaction_unknown_flag();

    t_procmask_block_unblock_setmask();
    t_procmask_kill_stop_unmaskable();
    t_procmask_oldset();
    t_procmask_bad_how();
    t_procmask_bad_sigsetsize();

    t_sigreturn_enosys();

    t_sigaltstack_basic();
    t_sigaltstack_too_small();
    t_sigaltstack_disable_skips_size();
    t_sigaltstack_old_ss();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
