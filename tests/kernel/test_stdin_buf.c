/*
 * Host tests for the M5 phase E.1 kernel stdin ring buffer.
 *
 * Coverage:
 *   - init: buffer empty, drop counter zero.
 *   - basic push/pop FIFO ordering.
 *   - empty pop returns 0 without touching *out.
 *   - NULL pop is a no-op.
 *   - overflow drops the LATEST byte (not the oldest) so previously
 *     buffered input survives and the drop counter advances.
 *   - count tracks live occupancy through interleaved push/pop.
 *   - capacity, free-space and high-watermark diagnostics track
 *     backpressure without changing FIFO semantics.
 *   - wrap-around: filling, draining, refilling re-uses slots
 *     correctly without corruption.
 */

#include <stdio.h>
#include <string.h>

#include "kernel/stdin_buf.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void test_init_state(void) {
    stdin_buf_init();

    TEST("stdin_buf_init: count == 0");
    if (stdin_buf_count() == 0) PASS(); else FAIL("count");

    TEST("stdin_buf_init: drop_total == 0");
    if (stdin_buf_dropped_total() == 0) PASS(); else FAIL("drop");

    TEST("stdin_buf_init: capacity and free space match size");
    if (stdin_buf_capacity() == STDIN_BUF_SIZE &&
        stdin_buf_space_available() == STDIN_BUF_SIZE) PASS();
    else FAIL("capacity/free");

    TEST("stdin_buf_init: high watermark == 0");
    if (stdin_buf_high_watermark() == 0) PASS(); else FAIL("hwm");

    char c = 0x55;
    TEST("stdin_buf_pop: empty buffer returns 0");
    if (stdin_buf_pop(&c) == 0) PASS(); else FAIL("non-zero rc");

    TEST("stdin_buf_pop: empty buffer leaves *out untouched");
    if (c == 0x55) PASS(); else FAIL("touched");
}

static void test_basic_fifo(void) {
    stdin_buf_init();

    TEST("stdin_buf_push: returns 1 on success");
    if (stdin_buf_push('a') == 1 &&
        stdin_buf_push('b') == 1 &&
        stdin_buf_push('c') == 1) PASS();
    else FAIL("push rc");

    TEST("stdin_buf_count: tracks pushed bytes");
    if (stdin_buf_count() == 3) PASS();
    else FAIL("count != 3");

    TEST("stdin_buf_space_available: tracks pushed bytes");
    if (stdin_buf_space_available() == STDIN_BUF_SIZE - 3u) PASS();
    else FAIL("space");

    TEST("stdin_buf_high_watermark: tracks peak occupancy");
    if (stdin_buf_high_watermark() == 3) PASS();
    else FAIL("hwm");

    char c = 0;
    TEST("stdin_buf_pop: FIFO order (a, b, c)");
    if (stdin_buf_pop(&c) == 1 && c == 'a' &&
        stdin_buf_pop(&c) == 1 && c == 'b' &&
        stdin_buf_pop(&c) == 1 && c == 'c') PASS();
    else FAIL("order");

    TEST("stdin_buf_count: zero after full drain");
    if (stdin_buf_count() == 0) PASS();
    else FAIL("count");

    TEST("stdin_buf_high_watermark: survives drain");
    if (stdin_buf_high_watermark() == 3) PASS();
    else FAIL("hwm reset");
}

static void test_ready(void) {
    stdin_buf_init();

    TEST("stdin_buf_ready: false after init");
    if (stdin_buf_ready() == 0) PASS();
    else FAIL("ready");

    stdin_buf_push('r');
    TEST("stdin_buf_ready: true after push without consuming");
    if (stdin_buf_ready() == 1 && stdin_buf_count() == 1) PASS();
    else FAIL("ready/count");

    char c = 0;
    stdin_buf_pop(&c);
    TEST("stdin_buf_ready: false after drain");
    if (stdin_buf_ready() == 0 && stdin_buf_count() == 0) PASS();
    else FAIL("drain");

    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) stdin_buf_push('x');
    TEST("stdin_buf_ready: true when full");
    if (stdin_buf_ready() == 1 && stdin_buf_count() == STDIN_BUF_SIZE) PASS();
    else FAIL("full");
}

static void test_null_pop(void) {
    stdin_buf_init();
    stdin_buf_push('x');

    TEST("stdin_buf_pop(NULL): no-op, returns 0");
    if (stdin_buf_pop(0) == 0) PASS();
    else FAIL("non-zero rc");

    TEST("stdin_buf_pop(NULL): leaves buffer untouched");
    if (stdin_buf_count() == 1) PASS();
    else FAIL("count drained");
}

static void test_pop_many(void) {
    stdin_buf_init();
    stdin_buf_push('a');
    stdin_buf_push('b');
    stdin_buf_push('c');
    stdin_buf_push('d');
    char out[4] = {0};

    TEST("stdin_buf_pop_many: NULL/zero are no-op");
    if (stdin_buf_pop_many(0, 2) == 0 &&
        stdin_buf_pop_many(out, 0) == 0 &&
        stdin_buf_count() == 4) PASS();
    else FAIL("null/zero");

    TEST("stdin_buf_pop_many: limited drain preserves FIFO prefix");
    if (stdin_buf_pop_many(out, 2) == 2 &&
        out[0] == 'a' && out[1] == 'b' &&
        stdin_buf_count() == 2) PASS();
    else FAIL("limited drain");

    TEST("stdin_buf_pop_many: drains remaining bytes");
    if (stdin_buf_pop_many(out, sizeof(out)) == 2 &&
        out[0] == 'c' && out[1] == 'd' &&
        stdin_buf_count() == 0) PASS();
    else FAIL("remaining drain");

    stdin_buf_init();
    for (size_t i = 0; i < STDIN_BUF_SIZE - 1u; i++) stdin_buf_push((char)('A' + (int)(i % 26)));
    char sink[STDIN_BUF_SIZE];
    stdin_buf_pop_many(sink, STDIN_BUF_SIZE - 2u);
    stdin_buf_push('x');
    stdin_buf_push('y');
    TEST("stdin_buf_pop_many: drains across wrap boundary");
    if (stdin_buf_pop_many(out, 3) == 3 &&
        out[0] == (char)('A' + (int)((STDIN_BUF_SIZE - 2u) % 26u)) &&
        out[1] == 'x' && out[2] == 'y' &&
        stdin_buf_count() == 0) PASS();
    else FAIL("wrap drain");
}

static void test_discard_many(void) {
    stdin_buf_init();
    stdin_buf_push('a');
    stdin_buf_push('b');
    stdin_buf_push('c');
    stdin_buf_push('d');

    TEST("stdin_buf_discard_many: zero is no-op");
    if (stdin_buf_discard_many(0) == 0 && stdin_buf_count() == 4) PASS();
    else FAIL("zero discard");

    TEST("stdin_buf_discard_many: limited discard preserves later FIFO");
    char c = 0;
    if (stdin_buf_discard_many(2) == 2 &&
        stdin_buf_count() == 2 &&
        stdin_buf_pop(&c) == 1 && c == 'c') PASS();
    else FAIL("limited discard");

    TEST("stdin_buf_discard_many: oversized discard drains remaining");
    if (stdin_buf_discard_many(STDIN_BUF_SIZE) == 1 &&
        stdin_buf_count() == 0 &&
        stdin_buf_ready() == 0) PASS();
    else FAIL("oversized discard");

    stdin_buf_init();
    for (size_t i = 0; i < STDIN_BUF_SIZE - 1u; i++) stdin_buf_push((char)('A' + (int)(i % 26)));
    stdin_buf_discard_many(STDIN_BUF_SIZE - 2u);
    stdin_buf_push('x');
    stdin_buf_push('y');
    TEST("stdin_buf_discard_many: discard across wrap preserves next bytes");
    if (stdin_buf_discard_many(1) == 1 &&
        stdin_buf_pop(&c) == 1 && c == 'x' &&
        stdin_buf_pop(&c) == 1 && c == 'y' &&
        stdin_buf_count() == 0) PASS();
    else FAIL("wrap discard");

    stdin_buf_init();
    stdin_buf_push('s');
    stdin_buf_push('e');
    TEST("stdin_buf_discard_all: drains complete backlog");
    if (stdin_buf_discard_all() == 2 &&
        stdin_buf_count() == 0 &&
        stdin_buf_ready() == 0) PASS();
    else FAIL("discard all");

    TEST("stdin_buf_discard_all: empty buffer is no-op");
    if (stdin_buf_discard_all() == 0 && stdin_buf_count() == 0) PASS();
    else FAIL("discard empty");
}

static void test_snapshot(void) {
    stdin_buf_init();

    TEST("stdin_buf_snapshot(NULL): returns 0");
    if (stdin_buf_snapshot(0) == 0) PASS();
    else FAIL("non-zero rc");

    struct stdin_buf_snapshot snap;
    TEST("stdin_buf_snapshot: init state is coherent");
    if (stdin_buf_snapshot(&snap) == 1 &&
        snap.capacity == STDIN_BUF_SIZE &&
        snap.count == 0 &&
        snap.space_available == STDIN_BUF_SIZE &&
        snap.high_watermark == 0 &&
        snap.dropped_total == 0) PASS();
    else FAIL("init snapshot");

    stdin_buf_push('a');
    stdin_buf_push('b');
    char c = 0;
    stdin_buf_pop(&c);
    TEST("stdin_buf_snapshot: active state is coherent");
    if (stdin_buf_snapshot(&snap) == 1 &&
        snap.capacity == STDIN_BUF_SIZE &&
        snap.count == 1 &&
        snap.space_available == STDIN_BUF_SIZE - 1u &&
        snap.high_watermark == 2 &&
        snap.dropped_total == 0) PASS();
    else FAIL("active snapshot");

    for (size_t i = 0; i < STDIN_BUF_SIZE - 1u; i++) stdin_buf_push('x');
    stdin_buf_push('z');
    TEST("stdin_buf_snapshot: overflow state is coherent");
    if (stdin_buf_snapshot(&snap) == 1 &&
        snap.capacity == STDIN_BUF_SIZE &&
        snap.count == STDIN_BUF_SIZE &&
        snap.space_available == 0 &&
        snap.high_watermark == STDIN_BUF_SIZE &&
        snap.dropped_total == 1) PASS();
    else FAIL("overflow snapshot");
}

static void test_diagnostics_reset_window(void) {
    stdin_buf_init();
    stdin_buf_push('a');
    stdin_buf_push('b');
    stdin_buf_push('c');
    char c = 0;
    stdin_buf_pop(&c);
    stdin_buf_reset_diagnostics();

    struct stdin_buf_snapshot snap;
    stdin_buf_snapshot(&snap);
    TEST("stdin_buf_reset_diagnostics: preserves count and rebases hwm");
    if (snap.count == 2 && snap.high_watermark == 2 &&
        snap.dropped_total == 0) PASS();
    else FAIL("reset window");

    TEST("stdin_buf_reset_diagnostics: preserves FIFO contents");
    if (stdin_buf_pop(&c) == 1 && c == 'b' &&
        stdin_buf_pop(&c) == 1 && c == 'c' &&
        stdin_buf_count() == 0) PASS();
    else FAIL("fifo changed");

    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) stdin_buf_push('x');
    stdin_buf_push('z');
    stdin_buf_reset_diagnostics();
    stdin_buf_snapshot(&snap);
    TEST("stdin_buf_reset_diagnostics: clears drops without clearing full buffer");
    if (snap.count == STDIN_BUF_SIZE &&
        snap.space_available == 0 &&
        snap.high_watermark == STDIN_BUF_SIZE &&
        snap.dropped_total == 0) PASS();
    else FAIL("full reset");

    stdin_buf_pop(&c);
    stdin_buf_push('y');
    stdin_buf_push('z');
    TEST("stdin_buf_reset_diagnostics: new overflow counted from reset window");
    if (stdin_buf_dropped_total() == 1) PASS();
    else FAIL("drop window");
}

static void test_overflow(void) {
    stdin_buf_init();

    /* Fill exactly STDIN_BUF_SIZE bytes. */
    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) {
        stdin_buf_push((char)('A' + (int)(i % 26)));
    }

    TEST("stdin_buf_push: count == STDIN_BUF_SIZE after fill");
    if (stdin_buf_count() == STDIN_BUF_SIZE) PASS();
    else FAIL("count");

    TEST("stdin_buf_space_available: zero when full");
    if (stdin_buf_space_available() == 0) PASS();
    else FAIL("space");

    TEST("stdin_buf_high_watermark: reaches full capacity");
    if (stdin_buf_high_watermark() == STDIN_BUF_SIZE) PASS();
    else FAIL("hwm");

    TEST("stdin_buf_push: returns 0 on overflow");
    if (stdin_buf_push('Z') == 0) PASS();
    else FAIL("rc");

    TEST("stdin_buf_dropped_total: increments on overflow");
    if (stdin_buf_dropped_total() == 1) PASS();
    else FAIL("drop");

    TEST("stdin_buf_push: 5 more drops accumulate");
    stdin_buf_push('Y');
    stdin_buf_push('Y');
    stdin_buf_push('Y');
    stdin_buf_push('Y');
    stdin_buf_push('Y');
    if (stdin_buf_dropped_total() == 6) PASS();
    else FAIL("drop count");

    TEST("stdin_buf_high_watermark: overflow does not exceed capacity");
    if (stdin_buf_high_watermark() == STDIN_BUF_SIZE) PASS();
    else FAIL("hwm overflow");

    char c = 0;
    TEST("stdin_buf_pop: head byte is the oldest pushed (FIFO survives overflow)");
    if (stdin_buf_pop(&c) == 1 && c == 'A') PASS();
    else FAIL("oldest lost");

    TEST("stdin_buf_space_available: pop after full frees one slot");
    if (stdin_buf_space_available() == 1) PASS();
    else FAIL("space after pop");
}

static void test_wraparound(void) {
    stdin_buf_init();

    /* Push N, drain N, push N+M -- exercise the modulo wrap. */
    const size_t step = (STDIN_BUF_SIZE * 3u) / 4u;
    for (size_t i = 0; i < step; i++) stdin_buf_push((char)(i & 0x7F));

    char c = 0;
    for (size_t i = 0; i < step; i++) {
        if (stdin_buf_pop(&c) != 1 || c != (char)(i & 0x7F)) {
            TEST("stdin_buf wrap: first drain matches push order");
            FAIL("mismatch");
            return;
        }
    }
    TEST("stdin_buf wrap: first drain matches push order");
    PASS();

    TEST("stdin_buf wrap: count zero between phases");
    if (stdin_buf_count() == 0) PASS();
    else FAIL("count");

    /* Now push a full buffer worth, crossing the wrap point. */
    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) {
        stdin_buf_push((char)('a' + (int)(i % 26)));
    }
    TEST("stdin_buf wrap: full re-fill across modulo boundary");
    if (stdin_buf_count() == STDIN_BUF_SIZE) PASS();
    else FAIL("count");

    /* Drain in order. */
    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) {
        char want = (char)('a' + (int)(i % 26));
        if (stdin_buf_pop(&c) != 1 || c != want) {
            TEST("stdin_buf wrap: drain after wrap preserves order");
            FAIL("mismatch");
            return;
        }
    }
    TEST("stdin_buf wrap: drain after wrap preserves order");
    PASS();
}

int test_stdin_buf_run(void) {
    printf("[test_stdin_buf]\n");
    tests_run = 0;
    tests_passed = 0;
    test_init_state();
    test_basic_fifo();
    test_ready();
    test_null_pop();
    test_pop_many();
    test_discard_many();
    test_snapshot();
    test_diagnostics_reset_window();
    test_overflow();
    test_wraparound();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
