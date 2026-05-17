/*
 * Host tests for linux_futex (S1.5).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_futex.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake ops for deterministic testing. */
static int g_atomic_load_calls;
static uint32_t g_atomic_load_value;
static int g_atomic_load_should_fail;

static int g_block_calls;
static uint64_t g_last_block_timeout;
static int g_block_return_value = LINUX_FUTEX_BLOCK_WOKEN;
static const uint32_t *g_last_block_uaddr;

static int g_wake_calls;
static int g_wake_max_seen;
static int g_wake_return_value = 1;

static int fake_atomic_load(const uint32_t *uaddr, uint32_t *out) {
    (void)uaddr;
    g_atomic_load_calls++;
    if (g_atomic_load_should_fail) return -1;
    *out = g_atomic_load_value;
    return 0;
}
static int fake_block(const uint32_t *uaddr, uint64_t timeout_ns) {
    g_block_calls++;
    g_last_block_timeout = timeout_ns;
    g_last_block_uaddr = uaddr;
    return g_block_return_value;
}
static int fake_wake(const uint32_t *uaddr, int max_waiters) {
    (void)uaddr;
    g_wake_calls++;
    g_wake_max_seen = max_waiters;
    return g_wake_return_value;
}

static void install_fake(void) {
    linux_futex_reset_for_tests();
    g_atomic_load_calls = g_block_calls = g_wake_calls = 0;
    g_atomic_load_value = 0;
    g_atomic_load_should_fail = 0;
    g_last_block_timeout = 0;
    g_last_block_uaddr = NULL;
    g_block_return_value = LINUX_FUTEX_BLOCK_WOKEN;
    g_wake_max_seen = 0;
    g_wake_return_value = 1;
    static const struct linux_futex_ops ops = {
        .atomic_load_u32 = fake_atomic_load,
        .block_on        = fake_block,
        .wake            = fake_wake,
    };
    linux_futex_install_ops(&ops);
}

/* aligned u32 storage */
static uint32_t g_word_a;
static uint32_t g_word_b;

/* -------- WAIT -------- */

static void t_wait_value_match_blocks(void) {
    install_fake();
    g_atomic_load_value = 42;
    g_word_a = 42;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 42, 0, NULL, 0);
    TEST("FUTEX_WAIT: value matches -> calls block_on, returns 0 on wake");
    if (r == 0 && g_block_calls == 1) PASS();
    else FAIL("did not block");
}

static void t_wait_value_mismatch_eagain(void) {
    install_fake();
    g_atomic_load_value = 99;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 42, 0, NULL, 0);
    TEST("FUTEX_WAIT: value mismatch -> -EAGAIN, no block");
    if (r == -LINUX_EAGAIN && g_block_calls == 0) PASS();
    else FAIL("did not return EAGAIN");
}

static void t_wait_efault_misaligned(void) {
    install_fake();
    /* misaligned ptr */
    uint32_t *bad = (uint32_t *)((uintptr_t)&g_word_a | 0x3u);
    int64_t r = linux_futex(bad, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    TEST("FUTEX_WAIT: misaligned uaddr -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("misaligned not rejected");
}

static void t_wait_null_uaddr(void) {
    install_fake();
    int64_t r = linux_futex(NULL, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    TEST("FUTEX_WAIT: NULL uaddr -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_wait_atomic_efault(void) {
    install_fake();
    g_atomic_load_should_fail = 1;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    TEST("FUTEX_WAIT: atomic_load fails -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("load fail not surfaced");
}

static void t_wait_timedout(void) {
    install_fake();
    g_atomic_load_value = 7;
    g_block_return_value = LINUX_FUTEX_BLOCK_TIMEDOUT;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 7, 1000000ull, NULL, 0);
    TEST("FUTEX_WAIT: block timeout -> -ETIMEDOUT");
    if (r == -LINUX_ETIMEDOUT && g_last_block_timeout == 1000000ull) PASS();
    else FAIL("timeout not surfaced");
}

static void t_wait_intr(void) {
    install_fake();
    g_atomic_load_value = 7;
    g_block_return_value = LINUX_FUTEX_BLOCK_INTR;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 7, 0, NULL, 0);
    TEST("FUTEX_WAIT: block interrupted -> -EINTR");
    if (r == -LINUX_EINTR) PASS();
    else FAIL("intr not surfaced");
}

static void t_wait_no_ops(void) {
    linux_futex_reset_for_tests();
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    TEST("FUTEX_WAIT: no ops installed -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("no-ops not surfaced");
}

static void t_install_null_clears_wait_ops(void) {
    install_fake();
    linux_futex_install_ops(NULL);
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    TEST("futex install_ops(NULL) clears WAIT callbacks");
    if (r == -LINUX_ENOSYS &&
        g_atomic_load_calls == 0 && g_block_calls == 0) PASS();
    else FAIL("WAIT callbacks not cleared");
}

static void t_wait_bitset(void) {
    install_fake();
    g_atomic_load_value = 5;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAIT_BITSET, 5, 0, NULL, 0xFF);
    TEST("FUTEX_WAIT_BITSET: same path as WAIT");
    if (r == 0 && g_block_calls == 1) PASS();
    else FAIL("WAIT_BITSET diverged");
}

/* -------- WAKE -------- */

static void t_wake_basic(void) {
    install_fake();
    g_wake_return_value = 3;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAKE, 5, 0, NULL, 0);
    TEST("FUTEX_WAKE: returns wake count, calls wake with val");
    if (r == 3 && g_wake_calls == 1 && g_wake_max_seen == 5) PASS();
    else FAIL("wake basic wrong");
}

static void t_wake_negative_val(void) {
    install_fake();
    /* val is uint32_t; cast to (int) by linux_futex; simulate
     * a negative passed via a huge uint32 (Linux handles this
     * by treating it as int -- 0x80000000 -> negative). */
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAKE, 0x80000000u, 0, NULL, 0);
    TEST("FUTEX_WAKE: negative val (cast int) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("negative not rejected");
}

static void t_wake_bitset(void) {
    install_fake();
    g_wake_return_value = 1;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAKE_BITSET, 1, 0, NULL, 0xFFu);
    TEST("FUTEX_WAKE_BITSET: same path as WAKE");
    if (r == 1) PASS();
    else FAIL("WAKE_BITSET diverged");
}

static void t_install_null_clears_wake_ops(void) {
    install_fake();
    linux_futex_install_ops(NULL);
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_WAKE, 1, 0, NULL, 0);
    TEST("futex install_ops(NULL) clears WAKE callback");
    if (r == -LINUX_ENOSYS && g_wake_calls == 0) PASS();
    else FAIL("WAKE callback not cleared");
}

/* -------- REQUEUE -------- */

static void t_requeue_basic(void) {
    install_fake();
    g_wake_return_value = 2;
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_REQUEUE, 2, 0, &g_word_b, 0);
    TEST("FUTEX_REQUEUE: wake `val` from uaddr1 (Marco M1 simplification)");
    if (r == 2 && g_wake_calls == 1) PASS();
    else FAIL("requeue wrong");
}

static void t_requeue_misaligned_uaddr2(void) {
    install_fake();
    uint32_t *bad = (uint32_t *)((uintptr_t)&g_word_b | 0x1u);
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_REQUEUE, 1, 0, bad, 0);
    TEST("FUTEX_REQUEUE: misaligned uaddr2 -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("uaddr2 alignment not checked");
}

static void t_install_null_clears_requeue_ops(void) {
    install_fake();
    linux_futex_install_ops(NULL);
    int64_t r = linux_futex(&g_word_a, LINUX_FUTEX_REQUEUE, 1, 0, &g_word_b, 0);
    TEST("futex install_ops(NULL) clears REQUEUE wake callback");
    if (r == -LINUX_ENOSYS && g_wake_calls == 0) PASS();
    else FAIL("REQUEUE callback not cleared");
}

/* -------- Flag handling -------- */

static void t_private_flag_accepted(void) {
    install_fake();
    g_atomic_load_value = 0;
    int64_t r = linux_futex(&g_word_a,
                            LINUX_FUTEX_WAIT | LINUX_FUTEX_PRIVATE_FLAG,
                            0, 0, NULL, 0);
    TEST("WAIT | PRIVATE_FLAG: accepted, same code path");
    if (r == 0) PASS();
    else FAIL("PRIVATE flag rejected");
}

static void t_clock_realtime_flag_accepted(void) {
    install_fake();
    g_atomic_load_value = 0;
    int64_t r = linux_futex(&g_word_a,
                            LINUX_FUTEX_WAIT_BITSET | LINUX_FUTEX_CLOCK_REALTIME,
                            0, 0, NULL, 0);
    TEST("WAIT_BITSET | CLOCK_REALTIME: accepted");
    if (r == 0) PASS();
    else FAIL("CLOCK_REALTIME rejected");
}

static void t_unknown_flag_rejected(void) {
    install_fake();
    int64_t r = linux_futex(&g_word_a,
                            LINUX_FUTEX_WAIT | 0x1000,
                            0, 0, NULL, 0);
    TEST("FUTEX flag bit outside known mask -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

/* -------- Unsupported ops -------- */

static void t_unsupported_ops_enosys(void) {
    install_fake();
    int64_t r1 = linux_futex(&g_word_a, LINUX_FUTEX_FD, 0, 0, NULL, 0);
    int64_t r2 = linux_futex(&g_word_a, LINUX_FUTEX_LOCK_PI, 0, 0, NULL, 0);
    int64_t r3 = linux_futex(&g_word_a, LINUX_FUTEX_WAKE_OP, 0, 0, NULL, 0);
    int64_t r4 = linux_futex(&g_word_a, LINUX_FUTEX_CMP_REQUEUE, 0, 0, NULL, 0);
    TEST("FUTEX_FD/LOCK_PI/WAKE_OP/CMP_REQUEUE -> -ENOSYS");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        r3 == -LINUX_ENOSYS && r4 == -LINUX_ENOSYS) PASS();
    else FAIL("unsupported did not return ENOSYS");
}

static void t_unknown_op_einval(void) {
    install_fake();
    int64_t r = linux_futex(&g_word_a, 99, 0, 0, NULL, 0);
    TEST("FUTEX unknown op (99) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown op accepted");
}

static void t_reset_clears_futex_callbacks(void) {
    install_fake();
    linux_futex_reset_for_tests();
    int64_t r1 = linux_futex(&g_word_a, LINUX_FUTEX_WAIT, 0, 0, NULL, 0);
    int64_t r2 = linux_futex(&g_word_a, LINUX_FUTEX_WAKE, 1, 0, NULL, 0);
    TEST("futex reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_atomic_load_calls == 0 && g_block_calls == 0 && g_wake_calls == 0) PASS();
    else FAIL("reset callbacks not cleared");
}

int test_linux_futex_run(void) {
    printf("[test_linux_futex]\n");
    tests_run = tests_passed = 0;

    t_wait_value_match_blocks();
    t_wait_value_mismatch_eagain();
    t_wait_efault_misaligned();
    t_wait_null_uaddr();
    t_wait_atomic_efault();
    t_wait_timedout();
    t_wait_intr();
    t_wait_no_ops();
    t_install_null_clears_wait_ops();
    t_wait_bitset();

    t_wake_basic();
    t_wake_negative_val();
    t_wake_bitset();
    t_install_null_clears_wake_ops();

    t_requeue_basic();
    t_requeue_misaligned_uaddr2();
    t_install_null_clears_requeue_ops();

    t_private_flag_accepted();
    t_clock_realtime_flag_accepted();
    t_unknown_flag_rejected();

    t_unsupported_ops_enosys();
    t_unknown_op_einval();
    t_reset_clears_futex_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
