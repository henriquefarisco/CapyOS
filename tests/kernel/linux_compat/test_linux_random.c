/*
 * Host tests for the Linux-ABI getrandom shim
 * (`include/kernel/linux_compat/linux_random.h` /
 *  `src/kernel/linux_compat/linux_random.c`).
 *
 * The CSPRNG itself is exercised in test_csprng.c. Here we only
 * lock the Linux-ABI surface: flag validation, length clipping,
 * NULL handling, source-not-installed semantics, and that bytes
 * actually come from the injected source.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_random.h"
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

/* Deterministic test source: writes ascending bytes 0,1,2,3,... */
static size_t g_source_bytes_written;
static void counter_source(void *buf, size_t len) {
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint8_t)((g_source_bytes_written + i) & 0xFF);
    }
    g_source_bytes_written += len;
}

static void install_counter_source(void) {
    linux_random_reset_for_tests();
    g_source_bytes_written = 0;
    linux_random_install_source(counter_source);
}

/* --------------------------------------------------------------------- */

static void test_no_source_returns_eagain(void) {
    linux_random_reset_for_tests();
    uint8_t buf[16] = {0xFF};
    int64_t r = linux_getrandom(buf, sizeof(buf), 0);

    TEST("getrandom: no source installed returns -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("expected -EAGAIN before install_source");
}

static void test_zero_len_returns_zero(void) {
    install_counter_source();
    uint8_t buf[16] = {0xFF};
    int64_t r1 = linux_getrandom(buf, 0, 0);
    int64_t r2 = linux_getrandom(NULL, 0, 0);

    TEST("getrandom: len=0 returns 0 even with NULL buf (no fault)");
    if (r1 == 0 && r2 == 0 && buf[0] == 0xFF) PASS();
    else FAIL("len=0 corrupted buf or returned non-zero");
}

static void test_null_buf_returns_efault(void) {
    install_counter_source();
    int64_t r = linux_getrandom(NULL, 16, 0);

    TEST("getrandom: NULL buf with len > 0 returns -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("expected -EFAULT for NULL buf");
}

static void test_unknown_flag_returns_einval(void) {
    install_counter_source();
    uint8_t buf[16];
    int64_t r1 = linux_getrandom(buf, sizeof(buf), 0x80000000u);
    int64_t r2 = linux_getrandom(buf, sizeof(buf), 0x100);  /* outside mask */
    int64_t r3 = linux_getrandom(buf, sizeof(buf), 0x10);   /* outside mask */

    TEST("getrandom: unknown flag bits return -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL && r3 == -LINUX_EINVAL)
        PASS();
    else FAIL("did not reject unknown flags");
}

static void test_known_flags_accepted(void) {
    install_counter_source();
    uint8_t buf[8];
    int64_t r1 = linux_getrandom(buf, sizeof(buf), 0);
    int64_t r2 = linux_getrandom(buf, sizeof(buf), LINUX_GRND_NONBLOCK);
    int64_t r3 = linux_getrandom(buf, sizeof(buf), LINUX_GRND_RANDOM);
    int64_t r4 = linux_getrandom(buf, sizeof(buf), LINUX_GRND_INSECURE);
    int64_t r5 = linux_getrandom(buf, sizeof(buf),
                                 LINUX_GRND_NONBLOCK | LINUX_GRND_INSECURE);

    TEST("getrandom: GRND_NONBLOCK/RANDOM/INSECURE all accepted");
    if (r1 == 8 && r2 == 8 && r3 == 8 && r4 == 8 && r5 == 8) PASS();
    else FAIL("at least one known flag was rejected");
}

static void test_basic_fill(void) {
    install_counter_source();
    uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int64_t r = linux_getrandom(buf, sizeof(buf), 0);

    /* Counter source writes bytes 0..7. */
    int values_match = 1;
    for (int i = 0; i < 8; i++) {
        if (buf[i] != (uint8_t)i) { values_match = 0; break; }
    }
    TEST("getrandom: buf is filled by the injected source (deterministic)");
    if (r == 8 && values_match) PASS();
    else FAIL("buf was not populated by source");
}

static void test_consecutive_calls_consume_source(void) {
    install_counter_source();
    uint8_t buf1[4];
    uint8_t buf2[4];
    int64_t r1 = linux_getrandom(buf1, sizeof(buf1), 0);
    int64_t r2 = linux_getrandom(buf2, sizeof(buf2), 0);

    /* First call: bytes 0..3. Second call: bytes 4..7. */
    int ok = (r1 == 4 && r2 == 4 &&
              buf1[0] == 0 && buf1[3] == 3 &&
              buf2[0] == 4 && buf2[3] == 7);
    TEST("getrandom: consecutive calls advance source state");
    if (ok) PASS();
    else FAIL("consecutive calls did not advance source");
}

/* Sink source for clip-at-cap test: counts bytes requested but never
 * writes into the user buffer. Lets us pass an absurd `len` without
 * crashing on a bogus pointer. */
static size_t g_sink_bytes_requested;
static void sink_source(void *buf, size_t len) {
    (void)buf;
    g_sink_bytes_requested += len;
}

static void test_clip_at_int_max(void) {
    /* Cap value matches Linux 6.x. */
    TEST("getrandom: LINUX_GETRANDOM_INT_MAX matches Linux 6.x (33554431)");
    if (LINUX_GETRANDOM_INT_MAX == 33554431u) PASS();
    else FAIL("cap value diverged from Linux 6.x");

    /* Behaviour: a request strictly larger than the cap must clip
     * the returned count to the cap, AND the source must be told to
     * fill exactly `cap` bytes, not the requested huge value. We
     * inject a sink source so we do not need to allocate 32 MiB. */
    linux_random_reset_for_tests();
    g_sink_bytes_requested = 0;
    linux_random_install_source(sink_source);

    /* Pass a sentinel `buf` -- the sink source ignores it. The shim
     * still requires `buf != NULL` for `len > 0`. */
    char dummy = 0;
    int64_t r = linux_getrandom(&dummy,
                                (size_t)LINUX_GETRANDOM_INT_MAX + 1024,
                                0);

    TEST("getrandom: len > cap clips to LINUX_GETRANDOM_INT_MAX bytes");
    if (r == (int64_t)LINUX_GETRANDOM_INT_MAX &&
        g_sink_bytes_requested == (size_t)LINUX_GETRANDOM_INT_MAX) PASS();
    else FAIL("did not clip the source request to the cap");
}

static void test_reset_for_tests_clears_source(void) {
    install_counter_source();
    uint8_t buf[4];
    int64_t r1 = linux_getrandom(buf, sizeof(buf), 0);

    linux_random_reset_for_tests();
    int64_t r2 = linux_getrandom(buf, sizeof(buf), 0);

    TEST("reset_for_tests: clears source -> next call is -EAGAIN");
    if (r1 == 4 && r2 == -LINUX_EAGAIN) PASS();
    else FAIL("reset incomplete");
}

static void test_install_null_clears_source(void) {
    install_counter_source();
    linux_random_install_source(NULL);
    uint8_t buf[4];
    int64_t r = linux_getrandom(buf, sizeof(buf), 0);

    TEST("install_source(NULL): clears source -> next call is -EAGAIN");
    if (r == -LINUX_EAGAIN && g_source_bytes_written == 0) PASS();
    else FAIL("stale random source invoked");
}

static void test_known_mask_constants(void) {
    /* Lock the constants against accidental edits. They MUST match
     * Linux 6.x include/uapi/linux/random.h. */
    TEST("flags: known constants match Linux upstream values");
    if (LINUX_GRND_NONBLOCK == 0x0001 &&
        LINUX_GRND_RANDOM   == 0x0002 &&
        LINUX_GRND_INSECURE == 0x0004 &&
        LINUX_GRND_KNOWN_MASK == 0x0007u) PASS();
    else FAIL("flag constants diverged");
}

static void test_partial_overlapping_flag_bits(void) {
    install_counter_source();
    uint8_t buf[8];
    int64_t r = linux_getrandom(buf, sizeof(buf),
                                LINUX_GRND_NONBLOCK |
                                LINUX_GRND_RANDOM |
                                LINUX_GRND_INSECURE);
    TEST("getrandom: combining all known flags is accepted");
    if (r == 8) PASS();
    else FAIL("rejected mask of all known flags");
}

int test_linux_random_run(void) {
    printf("[test_linux_random]\n");
    tests_run = 0;
    tests_passed = 0;

    test_no_source_returns_eagain();
    test_zero_len_returns_zero();
    test_null_buf_returns_efault();
    test_unknown_flag_returns_einval();
    test_known_flags_accepted();
    test_basic_fill();
    test_consecutive_calls_consume_source();
    test_clip_at_int_max();
    test_reset_for_tests_clears_source();
    test_install_null_clears_source();
    test_known_mask_constants();
    test_partial_overlapping_flag_bits();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
