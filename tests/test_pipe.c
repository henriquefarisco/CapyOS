/*
 * Host tests for the M5 phase D kernel pipe primitives
 * (`include/kernel/pipe.h` / `src/kernel/pipe.c`).
 *
 * The primitives are pure C (no asm, no platform-specific calls)
 * so the host build links them verbatim. We exercise:
 *
 *   - pipe_system_init zeros the table.
 *   - pipe_create returns sequential ids and flips both flags open.
 *   - pipe_create returns -1 once the table is full.
 *   - pipe_write fits up to PIPE_BUF_SIZE bytes; further bytes
 *     return -1 (would block).
 *   - pipe_read drains in FIFO order, returns 0 on drained-EOF
 *     after the write end has been closed, and -1 while the writer
 *     is still open with an empty buffer.
 *   - pipe_close_read short-circuits subsequent writes to -1
 *     (broken pipe).
 *   - pipe_close_write makes drained reads return 0 (EOF) instead
 *     of -1 (would block).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/pipe.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void test_create_basic(void) {
    pipe_system_init();

    int fds_a[2] = {-1, -1};
    int rc = pipe_create(fds_a);

    TEST("pipe_create: returns 0");
    if (rc == 0) PASS(); else FAIL("non-zero rc");

    TEST("pipe_create: fds[0] is the read end (id, NOT id+256)");
    if (fds_a[0] >= 0 && fds_a[0] < PIPE_MAX) PASS();
    else FAIL("fds[0] out of range");

    TEST("pipe_create: fds[1] is fds[0] + 256 (write end disambiguator)");
    if (fds_a[1] == fds_a[0] + 256) PASS();
    else FAIL("write end did not have +256 offset");
}

static void test_create_table_exhaustion(void) {
    pipe_system_init();

    /* Fill the table. */
    int fds[PIPE_MAX][2];
    for (int i = 0; i < PIPE_MAX; i++) {
        if (pipe_create(fds[i]) != 0) {
            TEST("pipe_create: filled table without premature failure");
            FAIL("returned -1 before table was full");
            return;
        }
    }
    int over[2];
    int rc = pipe_create(over);

    TEST("pipe_create: returns -1 once PIPE_MAX slots are taken");
    if (rc == -1) PASS();
    else FAIL("did not refuse over-allocation");
}

static void test_write_then_read(void) {
    pipe_system_init();
    int fds[2];
    pipe_create(fds);
    int pid = fds[0]; /* kernel-side id */

    const char msg[] = "hello,pipe";
    int wr = pipe_write(pid, msg, sizeof(msg));
    TEST("pipe_write: full payload accepted in one call");
    if (wr == (int)sizeof(msg)) PASS();
    else FAIL("partial or failed write");

    char buf[64];
    memset(buf, 0, sizeof(buf));
    int rd = pipe_read(pid, buf, sizeof(buf));
    TEST("pipe_read: returns the same byte count as written");
    if (rd == (int)sizeof(msg)) PASS();
    else FAIL("read length mismatch");

    TEST("pipe_read: byte content matches FIFO order");
    if (memcmp(buf, msg, sizeof(msg)) == 0) PASS();
    else FAIL("payload corruption");
}

static void test_read_empty_blocks(void) {
    pipe_system_init();
    int fds[2];
    pipe_create(fds);
    int pid = fds[0];

    char buf[16];
    int rd = pipe_read(pid, buf, sizeof(buf));
    TEST("pipe_read: empty buffer with writer open returns -1 (would block)");
    if (rd == -1) PASS();
    else FAIL("did not signal would-block");
}

static void test_eof_after_close_write(void) {
    pipe_system_init();
    int fds[2];
    pipe_create(fds);
    int pid = fds[0];

    pipe_close_write(pid);

    char buf[16];
    int rd = pipe_read(pid, buf, sizeof(buf));
    TEST("pipe_read: drained buffer after close_write returns 0 (EOF)");
    if (rd == 0) PASS();
    else FAIL("did not signal EOF");
}

static void test_broken_pipe_after_close_read(void) {
    pipe_system_init();
    int fds[2];
    pipe_create(fds);
    int pid = fds[0];

    pipe_close_read(pid);

    int wr = pipe_write(pid, "x", 1);
    TEST("pipe_write: returns -1 after close_read (broken pipe)");
    if (wr == -1) PASS();
    else FAIL("did not signal broken pipe");
}

static void test_buffer_full_blocks(void) {
    pipe_system_init();
    int fds[2];
    pipe_create(fds);
    int pid = fds[0];

    /* Fill exactly PIPE_BUF_SIZE bytes. */
    char buf[PIPE_BUF_SIZE];
    for (int i = 0; i < PIPE_BUF_SIZE; i++) buf[i] = (char)(i & 0xFF);
    int wr = pipe_write(pid, buf, PIPE_BUF_SIZE);
    TEST("pipe_write: accepts a full PIPE_BUF_SIZE payload");
    if (wr == PIPE_BUF_SIZE) PASS();
    else FAIL("did not accept full buffer");

    int wr2 = pipe_write(pid, "y", 1);
    TEST("pipe_write: returns -1 once buffer is full and not drained");
    if (wr2 == -1) PASS();
    else FAIL("did not signal would-block on full buffer");
}

int test_pipe_run(void) {
    printf("[test_pipe]\n");
    tests_run = 0;
    tests_passed = 0;
    test_create_basic();
    test_create_table_exhaustion();
    test_write_then_read();
    test_read_empty_blocks();
    test_eof_after_close_write();
    test_broken_pipe_after_close_read();
    test_buffer_full_blocks();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
