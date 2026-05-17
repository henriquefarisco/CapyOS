/*
 * Host tests for the Linux-ABI clock_gettime shim
 * (`include/kernel/linux_compat/linux_clock.h` /
 *  `src/kernel/linux_compat/linux_clock.c`).
 *
 * The shim is intentionally split into two layers:
 *
 *   1. linux_clock_compute_timespec(elapsed_cycles, hz, out)
 *      - pure arithmetic; no globals; testable in isolation
 *   2. linux_clock_gettime(clk, out)
 *      - dispatches on `clk` and reads cycles via injected callbacks
 *
 * These tests cover both layers: the arithmetic is hammered with edge
 * inputs (zero, sub-second, exact-second, 1 hour, 1 year of uptime,
 * tsc_hz boundary values) and the dispatcher is exercised against the
 * full `LINUX_CLOCK_*` switch with a deterministic injected timebase.
 *
 * Goal of the test suite: lock the contract that mfbt/TimeStamp_posix.cpp
 * and js/src/threading/posix/PosixThread.cpp expect from the kernel
 * under Strategy A.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_clock.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_types.h"

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
/* Injected timebase for layer 2 tests.                                  */
/* --------------------------------------------------------------------- */

static uint64_t g_test_cycles = 0;
static uint64_t g_test_hz = 0;

static uint64_t test_cycles_fn(void) { return g_test_cycles; }
static uint64_t test_hz_fn(void)     { return g_test_hz; }

static void install_test_timebase(uint64_t hz, uint64_t start_cycles) {
    linux_clock_reset_for_tests();
    g_test_hz = hz;
    g_test_cycles = start_cycles;
    linux_clock_install_timebase(test_cycles_fn, test_hz_fn, start_cycles);
}

/* --------------------------------------------------------------------- */
/* Layer 1: pure arithmetic.                                             */
/* --------------------------------------------------------------------- */

static void test_compute_zero_elapsed(void) {
    struct linux_timespec ts = {.tv_sec = 999, .tv_nsec = 999};
    int rc = linux_clock_compute_timespec(0, 1000000000ULL, &ts);

    TEST("compute_timespec: zero elapsed -> sec=0, nsec=0");
    if (rc == 0 && ts.tv_sec == 0 && ts.tv_nsec == 0) PASS();
    else FAIL("did not zero out");
}

static void test_compute_exact_one_second(void) {
    /* 3 GHz tsc, exactly 3e9 cycles == 1 second. */
    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(3000000000ULL, 3000000000ULL, &ts);

    TEST("compute_timespec: exactly one second tick");
    if (rc == 0 && ts.tv_sec == 1 && ts.tv_nsec == 0) PASS();
    else FAIL("did not produce sec=1, nsec=0");
}

static void test_compute_sub_second(void) {
    /* 3 GHz tsc, 1.5e9 cycles == 0.5 seconds == 500_000_000 ns. */
    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(1500000000ULL, 3000000000ULL, &ts);

    TEST("compute_timespec: 0.5 second -> sec=0, nsec=500_000_000");
    if (rc == 0 && ts.tv_sec == 0 && ts.tv_nsec == 500000000) PASS();
    else FAIL("sub-second decomposition wrong");
}

static void test_compute_nanosecond_precision(void) {
    /* 1 GHz tsc, 1 cycle == 1 ns. */
    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(1, 1000000000ULL, &ts);

    TEST("compute_timespec: 1 cycle @ 1 GHz == 1 ns");
    if (rc == 0 && ts.tv_sec == 0 && ts.tv_nsec == 1) PASS();
    else FAIL("did not preserve ns precision");
}

static void test_compute_long_uptime_no_overflow(void) {
    /* 1 year of uptime @ 4 GHz = 31_536_000 * 4e9 ~= 1.26e17 cycles
     * which is < 2^64 (~1.8e19). The naive `cycles*1e9` would overflow
     * within ~5 seconds; we should still be exact after a year. */
    uint64_t hz = 4000000000ULL;
    uint64_t one_year = 31536000ULL;
    uint64_t cycles = one_year * hz;

    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(cycles, hz, &ts);

    TEST("compute_timespec: 1 year of uptime decodes exactly");
    if (rc == 0 && ts.tv_sec == (linux_time_t)one_year && ts.tv_nsec == 0) PASS();
    else FAIL("decoded uptime drifted");
}

static void test_compute_remainder_within_second(void) {
    /* 1 GHz tsc + (5 * 1e9 + 250_000_000 / 4) cycles = 5 sec + something.
     * Let's pick 5_250_000_000 cycles @ 1 GHz = 5.25 sec. */
    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(5250000000ULL, 1000000000ULL, &ts);

    TEST("compute_timespec: 5.25s decomposes into 5s + 250_000_000 ns");
    if (rc == 0 && ts.tv_sec == 5 && ts.tv_nsec == 250000000) PASS();
    else FAIL("multi-second decomposition wrong");
}

static void test_compute_zero_hz_rejected(void) {
    struct linux_timespec ts = {0};
    int rc = linux_clock_compute_timespec(1000ULL, 0ULL, &ts);

    TEST("compute_timespec: tsc_hz=0 returns -EINVAL");
    if (rc == -LINUX_EINVAL) PASS();
    else FAIL("did not reject tsc_hz=0");
}

static void test_compute_null_out_rejected(void) {
    int rc = linux_clock_compute_timespec(1000ULL, 1000000000ULL, NULL);

    TEST("compute_timespec: NULL out returns -EFAULT");
    if (rc == -LINUX_EFAULT) PASS();
    else FAIL("did not reject NULL out");
}

static void test_add_basic(void) {
    struct linux_timespec a = {.tv_sec = 10, .tv_nsec = 100};
    struct linux_timespec b = {.tv_sec = 5,  .tv_nsec = 200};
    struct linux_timespec out = {0};
    int rc = linux_clock_add_timespec(&a, &b, &out);

    TEST("add_timespec: simple addition without normalisation");
    if (rc == 0 && out.tv_sec == 15 && out.tv_nsec == 300) PASS();
    else FAIL("did not add cleanly");
}

static void test_add_normalises_overflow(void) {
    struct linux_timespec a = {.tv_sec = 1, .tv_nsec = 800000000};
    struct linux_timespec b = {.tv_sec = 0, .tv_nsec = 500000000};
    struct linux_timespec out = {0};
    int rc = linux_clock_add_timespec(&a, &b, &out);

    /* 1.8 + 0.5 = 2.3 (== sec=2, nsec=300_000_000) */
    TEST("add_timespec: nsec overflow rolls into seconds");
    if (rc == 0 && out.tv_sec == 2 && out.tv_nsec == 300000000) PASS();
    else FAIL("did not normalise carry");
}

static void test_add_null_rejected(void) {
    struct linux_timespec a = {0};
    struct linux_timespec out = {0};

    TEST("add_timespec: NULL inputs all rejected with -EFAULT");
    int r1 = linux_clock_add_timespec(NULL, &a, &out);
    int r2 = linux_clock_add_timespec(&a, NULL, &out);
    int r3 = linux_clock_add_timespec(&a, &a, NULL);
    if (r1 == -LINUX_EFAULT && r2 == -LINUX_EFAULT && r3 == -LINUX_EFAULT) PASS();
    else FAIL("at least one NULL was not rejected");
}

/* --------------------------------------------------------------------- */
/* Layer 2: dispatcher.                                                  */
/* --------------------------------------------------------------------- */

static void test_gettime_null_out(void) {
    install_test_timebase(1000000000ULL, 0);
    int rc = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, NULL);

    TEST("clock_gettime: NULL out returns -EFAULT");
    if (rc == -LINUX_EFAULT) PASS();
    else FAIL("did not reject NULL out");
}

static void test_gettime_unknown_clock(void) {
    install_test_timebase(1000000000ULL, 0);
    struct linux_timespec ts = {0};
    int rc = linux_clock_gettime(99, &ts);

    TEST("clock_gettime: unknown clk_id -> -EINVAL");
    if (rc == -LINUX_EINVAL) PASS();
    else FAIL("did not reject unknown clk_id");
}

static void test_gettime_cputime_unimplemented(void) {
    install_test_timebase(1000000000ULL, 0);
    struct linux_timespec ts = {0};
    int rc1 = linux_clock_gettime(LINUX_CLOCK_PROCESS_CPUTIME_ID, &ts);
    int rc2 = linux_clock_gettime(LINUX_CLOCK_THREAD_CPUTIME_ID, &ts);

    TEST("clock_gettime: PROCESS/THREAD CPUTIME_ID -> -ENOSYS");
    if (rc1 == -LINUX_ENOSYS && rc2 == -LINUX_ENOSYS) PASS();
    else FAIL("did not return -ENOSYS for cputime");
}

static void test_gettime_pre_init_returns_zero(void) {
    linux_clock_reset_for_tests();
    struct linux_timespec ts = {.tv_sec = 9, .tv_nsec = 9};
    int rc = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &ts);

    TEST("clock_gettime: pre-init monotonic -> sec=0, nsec=0 (no crash)");
    if (rc == 0 && ts.tv_sec == 0 && ts.tv_nsec == 0) PASS();
    else FAIL("pre-init did not return zeros gracefully");
}

static void test_gettime_monotonic_increases(void) {
    /* 1 GHz; advance synthetic cycles between two reads. */
    install_test_timebase(1000000000ULL, /*start*/ 0);

    struct linux_timespec t1 = {0};
    g_test_cycles = 500000000ULL;       /* 0.5 sec */
    int rc1 = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &t1);

    struct linux_timespec t2 = {0};
    g_test_cycles = 1500000000ULL;      /* 1.5 sec */
    int rc2 = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &t2);

    TEST("clock_gettime: CLOCK_MONOTONIC strictly increases when cycles do");
    int t1_total_ns_ok = (t1.tv_sec == 0 && t1.tv_nsec == 500000000);
    int t2_total_ns_ok = (t2.tv_sec == 1 && t2.tv_nsec == 500000000);
    if (rc1 == 0 && rc2 == 0 && t1_total_ns_ok && t2_total_ns_ok) PASS();
    else FAIL("did not match expected ns values across two reads");
}

static void test_gettime_monotonic_aliases(void) {
    install_test_timebase(2000000000ULL, /*start*/ 100ULL);
    g_test_cycles = 100ULL + 6000000000ULL;  /* 3 seconds */

    struct linux_timespec a = {0};
    struct linux_timespec b = {0};
    struct linux_timespec c = {0};
    struct linux_timespec d = {0};

    int ra = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &a);
    int rb = linux_clock_gettime(LINUX_CLOCK_MONOTONIC_RAW, &b);
    int rc = linux_clock_gettime(LINUX_CLOCK_MONOTONIC_COARSE, &c);
    int rd = linux_clock_gettime(LINUX_CLOCK_BOOTTIME, &d);

    TEST("clock_gettime: MONOTONIC variants and BOOTTIME share the timebase");
    int values_match = (a.tv_sec == b.tv_sec && b.tv_sec == c.tv_sec &&
                        c.tv_sec == d.tv_sec &&
                        a.tv_nsec == b.tv_nsec && b.tv_nsec == c.tv_nsec &&
                        c.tv_nsec == d.tv_nsec);
    if (ra == 0 && rb == 0 && rc == 0 && rd == 0 && values_match &&
        a.tv_sec == 3 && a.tv_nsec == 0) PASS();
    else FAIL("monotonic variants diverged");
}

static void test_gettime_realtime_without_epoch(void) {
    /* No wall epoch installed -> CLOCK_REALTIME falls back to monotonic. */
    install_test_timebase(1000000000ULL, /*start*/ 0);
    g_test_cycles = 7000000000ULL;  /* 7 seconds */

    struct linux_timespec rt = {0};
    int rc = linux_clock_gettime(LINUX_CLOCK_REALTIME, &rt);

    TEST("clock_gettime: REALTIME without epoch falls back to monotonic");
    if (rc == 0 && rt.tv_sec == 7 && rt.tv_nsec == 0) PASS();
    else FAIL("REALTIME fallback diverged from MONOTONIC");
}

static void test_gettime_realtime_with_epoch(void) {
    /* Wall epoch = 2026-01-01T00:00:00 UTC = 1767225600 unix seconds.
     * Plus 7 seconds of monotonic = 1767225607. */
    install_test_timebase(1000000000ULL, /*start*/ 0);
    g_test_cycles = 7000000000ULL;
    linux_clock_install_wall_epoch(1767225600LL, 0LL);

    struct linux_timespec rt = {0};
    int rc = linux_clock_gettime(LINUX_CLOCK_REALTIME, &rt);

    TEST("clock_gettime: REALTIME = wall_epoch + monotonic");
    if (rc == 0 && rt.tv_sec == 1767225607 && rt.tv_nsec == 0) PASS();
    else FAIL("REALTIME did not add wall epoch");
}

static void test_gettime_realtime_with_partial_ns_epoch(void) {
    /* Wall epoch with 999_900_000 ns offset; monotonic 200_000_000 ns
     * later -> should carry into seconds: 999_900_000 + 200_000_000 =
     * 1_199_900_000 ns -> 1 sec + 199_900_000 ns. */
    install_test_timebase(1000000000ULL, /*start*/ 0);
    g_test_cycles = 200000000ULL;
    linux_clock_install_wall_epoch(1000LL, 999900000LL);

    struct linux_timespec rt = {0};
    int rc = linux_clock_gettime(LINUX_CLOCK_REALTIME, &rt);

    TEST("clock_gettime: REALTIME nsec carry across second boundary");
    if (rc == 0 && rt.tv_sec == 1001 && rt.tv_nsec == 199900000) PASS();
    else FAIL("nsec carry not normalised");
}

static void test_gettime_clamps_negative_elapsed(void) {
    /* Invariant: now < tsc_start should never produce negative time.
     * A buggy timebase that goes backwards must clamp to t=0 instead
     * of crashing or wrapping. */
    install_test_timebase(1000000000ULL, /*start*/ 1000000ULL);
    g_test_cycles = 999ULL;  /* now < tsc_start */

    struct linux_timespec ts = {.tv_sec = 5, .tv_nsec = 5};
    int rc = linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &ts);

    TEST("clock_gettime: now < tsc_start clamps to t=0 (no crash, no wrap)");
    if (rc == 0 && ts.tv_sec == 0 && ts.tv_nsec == 0) PASS();
    else FAIL("did not clamp backwards-running TSC");
}

static void test_reset_for_tests_clears_state(void) {
    /* Use values that produce an exact second to avoid sub-ns rounding
     * obscuring the real check (that reset clears all installed state). */
    install_test_timebase(1000000000ULL, /*start*/ 0);
    linux_clock_install_wall_epoch(123, 0);
    g_test_cycles = 4000000000ULL;  /* 4 seconds @ 1 GHz */

    /* With state installed, REALTIME == wall_epoch + monotonic. */
    struct linux_timespec before = {0};
    int rb = linux_clock_gettime(LINUX_CLOCK_REALTIME, &before);

    /* Reset and re-check: callbacks cleared, MONOTONIC pre-init = 0,
     * REALTIME falls back to MONOTONIC = 0. */
    linux_clock_reset_for_tests();
    struct linux_timespec after = {.tv_sec = 9, .tv_nsec = 9};
    int ra = linux_clock_gettime(LINUX_CLOCK_REALTIME, &after);

    TEST("reset_for_tests: clears callbacks and wall epoch");
    if (rb == 0 && before.tv_sec == 127 && before.tv_nsec == 0 &&
        ra == 0 && after.tv_sec == 0 && after.tv_nsec == 0) PASS();
    else FAIL("reset did not fully clear state");
}

/* -------- musl bring-up: gettimeofday + nanosleep (sessao 20) -------- */

static void t_gettimeofday_null_efault(void) {
    int64_t r = linux_gettimeofday(NULL);
    TEST("gettimeofday(NULL) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_gettimeofday_basic(void) {
    /* 1 GHz timebase: 1 cycle == 1 ns. 5_000_000_250 cycles =
     * 5.000000250 s = 5 s + 250 ns. tv_usec truncates ns/1000 = 0
     * (since 250 ns < 1 us). Adjust to 5_000_250_000 cycles ->
     * 5.00025 s -> tv_usec = 250. */
    install_test_timebase(1000000000ull, 0);
    g_test_cycles = 5000250000ull;

    struct linux_timeval tv;
    int64_t r = linux_gettimeofday(&tv);
    TEST("gettimeofday: ns=250000 -> tv_usec=250 (truncating ns/1000)");
    if (r == 0 && tv.tv_sec == 5 && tv.tv_usec == 250) PASS();
    else FAIL("conversion wrong");
}

static void t_nanosleep_null_efault(void) {
    int64_t r = linux_nanosleep(NULL, NULL);
    TEST("nanosleep(NULL, NULL) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_nanosleep_invalid_nsec(void) {
    /* nsec >= 1e9 is invalid. */
    struct linux_timespec req = { .tv_sec = 0, .tv_nsec = 1000000000 };
    int64_t r = linux_nanosleep(&req, NULL);
    TEST("nanosleep: nsec >= 1e9 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_nanosleep_negative_sec(void) {
    struct linux_timespec req = { .tv_sec = -1, .tv_nsec = 0 };
    int64_t r = linux_nanosleep(&req, NULL);
    TEST("nanosleep: tv_sec < 0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_nanosleep_zero_completes_immediately(void) {
    /* Constant timebase: req={0,0} -> deadline equals now, loop
     * exits first iteration without spinning. */
    install_test_timebase(1000000000ull, 0);

    struct linux_timespec req = {0};
    struct linux_timespec rem = { .tv_sec = 99, .tv_nsec = 99 };
    int64_t r = linux_nanosleep(&req, &rem);
    TEST("nanosleep: zero duration returns 0 with rem zeroed");
    if (r == 0 && rem.tv_sec == 0 && rem.tv_nsec == 0) PASS();
    else FAIL("not zero");
}

static void t_nanosleep_no_timebase_returns_zero(void) {
    /* No timebase installed -> nanosleep fast-returns. */
    linux_clock_reset_for_tests();
    struct linux_timespec req = { .tv_sec = 1, .tv_nsec = 0 };
    int64_t r = linux_nanosleep(&req, NULL);
    TEST("nanosleep without timebase: returns 0 immediately");
    if (r == 0) PASS();
    else FAIL("not zero");
}

int test_linux_clock_run(void) {
    printf("[test_linux_clock]\n");
    tests_run = 0;
    tests_passed = 0;

    /* Layer 1: pure arithmetic. */
    test_compute_zero_elapsed();
    test_compute_exact_one_second();
    test_compute_sub_second();
    test_compute_nanosecond_precision();
    test_compute_long_uptime_no_overflow();
    test_compute_remainder_within_second();
    test_compute_zero_hz_rejected();
    test_compute_null_out_rejected();
    test_add_basic();
    test_add_normalises_overflow();
    test_add_null_rejected();

    /* Layer 2: dispatcher. */
    test_gettime_null_out();
    test_gettime_unknown_clock();
    test_gettime_cputime_unimplemented();
    test_gettime_pre_init_returns_zero();
    test_gettime_monotonic_increases();
    test_gettime_monotonic_aliases();
    test_gettime_realtime_without_epoch();
    test_gettime_realtime_with_epoch();
    test_gettime_realtime_with_partial_ns_epoch();
    test_gettime_clamps_negative_elapsed();
    test_reset_for_tests_clears_state();

    /* musl bring-up syscalls (sessao 20). */
    t_gettimeofday_null_efault();
    t_gettimeofday_basic();
    t_nanosleep_null_efault();
    t_nanosleep_invalid_nsec();
    t_nanosleep_negative_sec();
    t_nanosleep_zero_completes_immediately();
    t_nanosleep_no_timebase_returns_zero();

    /* clock_nanosleep (sessao 27). */
    install_test_timebase(1000000000ull, 0);
    {
        struct linux_timespec req = { .tv_sec = 0, .tv_nsec = 0 };
        TEST("clock_nanosleep: NULL req -> -EFAULT");
        if (linux_clock_nanosleep(LINUX_CLOCK_MONOTONIC, 0, NULL, NULL)
            == -LINUX_EFAULT) PASS();
        else FAIL("EFAULT not surfaced");

        TEST("clock_nanosleep: unknown flags -> -EINVAL");
        if (linux_clock_nanosleep(LINUX_CLOCK_MONOTONIC, 0xDEAD, &req, NULL)
            == -LINUX_EINVAL) PASS();
        else FAIL("EINVAL not surfaced");

        TEST("clock_nanosleep: invalid clockid -> -EINVAL");
        if (linux_clock_nanosleep(99, 0, &req, NULL) == -LINUX_EINVAL) PASS();
        else FAIL("EINVAL not surfaced");

        TEST("clock_nanosleep: CPUTIME clocks -> -EOPNOTSUPP");
        if (linux_clock_nanosleep(LINUX_CLOCK_PROCESS_CPUTIME_ID,
                                  0, &req, NULL) == -LINUX_EOPNOTSUPP) PASS();
        else FAIL("EOPNOTSUPP not surfaced");

        TEST("clock_nanosleep: zero req completes immediately");
        if (linux_clock_nanosleep(LINUX_CLOCK_MONOTONIC, 0, &req, NULL)
            == 0) PASS();
        else FAIL("zero req not zero");

        TEST("clock_nanosleep: TIMER_ABSTIME at-now deadline returns 0");
        /* test timebase has cycles=0 -> now=(0,0). Use a deadline
         * already reached (sec=0, nsec=0) so the spin-wait exits
         * immediately on the first iteration. */
        struct linux_timespec at_now = { .tv_sec = 0, .tv_nsec = 0 };
        if (linux_clock_nanosleep(LINUX_CLOCK_MONOTONIC,
                                  LINUX_TIMER_ABSTIME, &at_now, NULL)
            == 0) PASS();
        else FAIL("ABSTIME at-now not zero");
    }

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
