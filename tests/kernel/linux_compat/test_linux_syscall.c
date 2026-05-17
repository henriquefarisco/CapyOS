/*
 * Host tests for the Linux-ABI syscall dispatcher
 * (`include/kernel/linux_compat/linux_syscall.h` /
 *  `src/kernel/linux_compat/linux_syscall.c`).
 *
 * The dispatcher is the gatekeeper for every Linux-ABI syscall in
 * the future: any handler that misroutes here defeats Strategy A.
 * The tests lock:
 *   - empty table -> -ENOSYS (NEVER -EINVAL: that would tell userland
 *     "your call is malformed", which is wrong -- the call shape is
 *     fine, we just have not implemented it yet).
 *   - registration semantics: refuse to overwrite, refuse NULL
 *     handler, refuse out-of-range NR.
 *   - lookup observability for tests.
 *   - dispatch propagates the args struct verbatim.
 *   - dispatch propagates the return value.
 *   - NULL args -> -EFAULT.
 *   - reset_for_tests clears state.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* --------------------------------------------------------------------- */
/* Test handlers used across multiple scenarios.                         */
/* --------------------------------------------------------------------- */

static struct linux_syscall_args g_seen_args;
static int g_handler_calls;

static int64_t echo_a0(const struct linux_syscall_args *a) {
    g_handler_calls++;
    g_seen_args = *a;
    return (int64_t)a->a0;
}

static int64_t echo_sum(const struct linux_syscall_args *a) {
    g_handler_calls++;
    return (int64_t)(a->a0 + a->a1 + a->a2 + a->a3 + a->a4 + a->a5);
}

static int64_t return_einval(const struct linux_syscall_args *a) {
    (void)a;
    return -LINUX_EINVAL;
}

static int64_t return_zero(const struct linux_syscall_args *a) {
    (void)a;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Tests.                                                                */
/* --------------------------------------------------------------------- */

static void test_empty_table_returns_enosys(void) {
    linux_syscall_reset_for_tests();
    struct linux_syscall_args args = {0};

    int64_t r1 = linux_syscall_dispatch(LINUX_NR_clock_gettime, &args);
    int64_t r2 = linux_syscall_dispatch(LINUX_NR_getrandom, &args);
    int64_t r3 = linux_syscall_dispatch(LINUX_NR_read, &args);

    TEST("dispatch: empty table returns -ENOSYS for any NR (not -EINVAL)");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS && r3 == -LINUX_ENOSYS) PASS();
    else FAIL("expected -ENOSYS across NRs");
}

static void test_dispatch_out_of_range_nr(void) {
    linux_syscall_reset_for_tests();
    struct linux_syscall_args args = {0};

    int64_t r = linux_syscall_dispatch(LINUX_NR_MAX, &args);
    int64_t r2 = linux_syscall_dispatch(0xFFFFFFFFu, &args);

    TEST("dispatch: NR >= LINUX_NR_MAX returns -ENOSYS");
    if (r == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS) PASS();
    else FAIL("expected -ENOSYS for out-of-range NR");
}

static void test_dispatch_null_args(void) {
    linux_syscall_reset_for_tests();
    int64_t r = linux_syscall_dispatch(LINUX_NR_clock_gettime, NULL);

    TEST("dispatch: NULL args returns -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("expected -EFAULT for NULL args");
}

static void test_register_basic(void) {
    linux_syscall_reset_for_tests();

    int rc = linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);
    int count = (int)linux_syscall_registered_count();

    TEST("register: basic install returns 0 and bumps counter to 1");
    if (rc == 0 && count == 1) PASS();
    else FAIL("rc != 0 or counter != 1");
}

static void test_register_refuses_null(void) {
    linux_syscall_reset_for_tests();
    int rc = linux_syscall_register(LINUX_NR_clock_gettime, NULL);

    TEST("register: NULL handler is rejected with -1");
    if (rc == -1 && linux_syscall_registered_count() == 0) PASS();
    else FAIL("did not refuse NULL handler");
}

static void test_register_refuses_out_of_range(void) {
    linux_syscall_reset_for_tests();
    int rc = linux_syscall_register(LINUX_NR_MAX, echo_a0);
    int rc2 = linux_syscall_register(LINUX_NR_MAX + 100, echo_a0);

    TEST("register: NR >= LINUX_NR_MAX is rejected with -1");
    if (rc == -1 && rc2 == -1) PASS();
    else FAIL("did not refuse out-of-range NR");
}

static void test_register_refuses_double_install(void) {
    linux_syscall_reset_for_tests();
    int rc1 = linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);
    int rc2 = linux_syscall_register(LINUX_NR_clock_gettime, echo_sum);

    TEST("register: refuses to overwrite an installed handler");
    if (rc1 == 0 && rc2 == -1 &&
        linux_syscall_lookup(LINUX_NR_clock_gettime) == echo_a0) PASS();
    else FAIL("either accepted overwrite or counter wrong");
}

static void test_lookup_returns_pointer(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_getrandom, echo_sum);

    linux_syscall_fn h = linux_syscall_lookup(LINUX_NR_getrandom);
    linux_syscall_fn miss = linux_syscall_lookup(LINUX_NR_clock_gettime);
    linux_syscall_fn oor = linux_syscall_lookup(LINUX_NR_MAX);

    TEST("lookup: returns handler for installed, NULL for unset/oor");
    if (h == echo_sum && miss == NULL && oor == NULL) PASS();
    else FAIL("lookup behaviour wrong");
}

static void test_dispatch_propagates_args(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);

    g_handler_calls = 0;
    struct linux_syscall_args args = {
        .a0 = 0xC0FFEE, .a1 = 0xDEADBEEF, .a2 = 1,
        .a3 = 2, .a4 = 3, .a5 = 4,
    };
    int64_t r = linux_syscall_dispatch(LINUX_NR_clock_gettime, &args);

    TEST("dispatch: args struct is propagated verbatim to handler");
    if (g_handler_calls == 1 && r == (int64_t)0xC0FFEE &&
        g_seen_args.a0 == 0xC0FFEE && g_seen_args.a1 == 0xDEADBEEF &&
        g_seen_args.a2 == 1 && g_seen_args.a3 == 2 &&
        g_seen_args.a4 == 3 && g_seen_args.a5 == 4) PASS();
    else FAIL("args mismatch or wrong call count");
}

static void test_dispatch_propagates_return(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_clock_gettime, return_einval);
    linux_syscall_register(LINUX_NR_getrandom, return_zero);

    struct linux_syscall_args args = {0};
    int64_t r1 = linux_syscall_dispatch(LINUX_NR_clock_gettime, &args);
    int64_t r2 = linux_syscall_dispatch(LINUX_NR_getrandom, &args);

    TEST("dispatch: handler return value flows through verbatim");
    if (r1 == -LINUX_EINVAL && r2 == 0) PASS();
    else FAIL("handler return values diverged");
}

static void test_dispatch_uses_full_a0_a5(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_clock_gettime, echo_sum);

    struct linux_syscall_args args = {
        .a0 = 1, .a1 = 2, .a2 = 4,
        .a3 = 8, .a4 = 16, .a5 = 32,
    };
    int64_t r = linux_syscall_dispatch(LINUX_NR_clock_gettime, &args);

    TEST("dispatch: all 6 args reach the handler (sum = 63)");
    if (r == 63) PASS();
    else FAIL("not all args summed");
}

static void test_register_multiple_handlers(void) {
    linux_syscall_reset_for_tests();
    int r1 = linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);
    int r2 = linux_syscall_register(LINUX_NR_getrandom, echo_sum);
    int r3 = linux_syscall_register(LINUX_NR_read, return_zero);

    TEST("register: 3 distinct NRs install cleanly, counter == 3");
    if (r1 == 0 && r2 == 0 && r3 == 0 &&
        linux_syscall_registered_count() == 3) PASS();
    else FAIL("multi-install failed");
}

static void test_init_idempotent(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_init();
    size_t after_first = linux_syscall_registered_count();
    linux_syscall_init();
    linux_syscall_init();
    size_t after_third = linux_syscall_registered_count();

    TEST("init: second/third call is a no-op (idempotent)");
    if (after_first == after_third) PASS();
    else FAIL("init was not idempotent");
}

static void test_init_populates_via_module_hooks(void) {
    /* When representative modules are linked into
     * the build (which is the case both in the kernel binary and in
     * this host test, see test_runner.c TEST_SRCS), `linux_syscall_init`
     * must call their `_register_syscalls` hooks and the table must
     * become non-empty. If a future module forgets to call
     * `linux_syscall_register` from its hook this test catches it. */
    linux_syscall_reset_for_tests();
    linux_syscall_init();
    size_t count = linux_syscall_registered_count();

    linux_syscall_fn h_clock = linux_syscall_lookup(LINUX_NR_clock_gettime);
    linux_syscall_fn h_random = linux_syscall_lookup(LINUX_NR_getrandom);
    linux_syscall_fn h_pipe = linux_syscall_lookup(LINUX_NR_pipe);

    TEST("init: linked modules populate the table (clock_gettime + getrandom + pipe)");
    if (count >= 3 && h_clock != NULL && h_random != NULL && h_pipe != NULL) PASS();
    else FAIL("init did not call all module hooks");
}

static void test_reset_for_tests_clears_state(void) {
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);
    linux_syscall_register(LINUX_NR_getrandom, echo_sum);

    linux_syscall_reset_for_tests();
    int count = (int)linux_syscall_registered_count();
    linux_syscall_fn h = linux_syscall_lookup(LINUX_NR_clock_gettime);
    int64_t r = linux_syscall_dispatch(LINUX_NR_clock_gettime,
                                       &(struct linux_syscall_args){0});

    TEST("reset_for_tests: clears handlers and resets counter");
    if (count == 0 && h == NULL && r == -LINUX_ENOSYS) PASS();
    else FAIL("reset incomplete");
}

static void test_dispatch_does_not_call_unregistered_neighbors(void) {
    /* If we register only NR_clock_gettime, dispatching NR_getrandom
     * must NOT spill into the clock_gettime handler. Locks the
     * "sparse table" invariant. */
    linux_syscall_reset_for_tests();
    linux_syscall_register(LINUX_NR_clock_gettime, echo_a0);

    g_handler_calls = 0;
    struct linux_syscall_args args = {.a0 = 42};
    int64_t r = linux_syscall_dispatch(LINUX_NR_getrandom, &args);

    TEST("dispatch: unregistered NR does NOT spill to neighbouring slot");
    if (r == -LINUX_ENOSYS && g_handler_calls == 0) PASS();
    else FAIL("dispatch spilled or wrong return");
}

static void test_high_nr_register_and_dispatch(void) {
    /* clone3 is NR=435, just under our LINUX_NR_MAX=512. Validates
     * the table is sized to cover modern Linux NRs. */
    linux_syscall_reset_for_tests();
    int rc = linux_syscall_register(LINUX_NR_clone3, echo_a0);

    struct linux_syscall_args args = {.a0 = 0xBADF00D};
    int64_t r = linux_syscall_dispatch(LINUX_NR_clone3, &args);

    TEST("register/dispatch: high NR (clone3=435) installs and dispatches");
    if (rc == 0 && r == (int64_t)0xBADF00D) PASS();
    else FAIL("high-NR slot mishandled");
}

int test_linux_syscall_run(void) {
    printf("[test_linux_syscall]\n");
    tests_run = 0;
    tests_passed = 0;

    test_empty_table_returns_enosys();
    test_dispatch_out_of_range_nr();
    test_dispatch_null_args();
    test_register_basic();
    test_register_refuses_null();
    test_register_refuses_out_of_range();
    test_register_refuses_double_install();
    test_lookup_returns_pointer();
    test_dispatch_propagates_args();
    test_dispatch_propagates_return();
    test_dispatch_uses_full_a0_a5();
    test_register_multiple_handlers();
    test_init_idempotent();
    test_init_populates_via_module_hooks();
    test_reset_for_tests_clears_state();
    test_dispatch_does_not_call_unregistered_neighbors();
    test_high_nr_register_and_dispatch();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
