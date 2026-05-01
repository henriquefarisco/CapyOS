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

    char c = 0;
    TEST("stdin_buf_pop: FIFO order (a, b, c)");
    if (stdin_buf_pop(&c) == 1 && c == 'a' &&
        stdin_buf_pop(&c) == 1 && c == 'b' &&
        stdin_buf_pop(&c) == 1 && c == 'c') PASS();
    else FAIL("order");

    TEST("stdin_buf_count: zero after full drain");
    if (stdin_buf_count() == 0) PASS();
    else FAIL("count");
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

static void test_overflow(void) {
    stdin_buf_init();

    /* Fill exactly STDIN_BUF_SIZE bytes. */
    for (size_t i = 0; i < STDIN_BUF_SIZE; i++) {
        stdin_buf_push((char)('A' + (int)(i % 26)));
    }

    TEST("stdin_buf_push: count == STDIN_BUF_SIZE after fill");
    if (stdin_buf_count() == STDIN_BUF_SIZE) PASS();
    else FAIL("count");

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

    char c = 0;
    TEST("stdin_buf_pop: head byte is the oldest pushed (FIFO survives overflow)");
    if (stdin_buf_pop(&c) == 1 && c == 'A') PASS();
    else FAIL("oldest lost");
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
    test_null_pop();
    test_overflow();
    test_wraparound();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
