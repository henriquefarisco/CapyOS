/*
 * Host test for userland/bin/hello (M4 phase 5b).
 *
 * Strategy:
 *   - Provide host-side stubs for the capylibc syscalls.
 *   - #include the user binary's main() under a renamed symbol so
 *     two TUs can coexist in the same test binary without colliding.
 *   - Call the renamed main() and verify the recorded SYS_WRITE
 *     argument list plus the return value.
 *
 * What this locks:
 *   - hello writes EXACTLY one capy_write call to fd 1 with the
 *     literal "hello, capyland\n" (16 bytes, no NUL).
 *   - hello returns 0 from main, so crt0.S forwards code 0 to
 *     SYS_EXIT. The kernel-side expectation in phase 5c is that the
 *     process leaves with exit_code == 0.
 *   - hello does NOT call any other syscall (no exit, no read, no
 *     yield); the user-visible behaviour is one line then return.
 *
 * The test does NOT validate the asm wiring (that lives in
 * tests/test_capylibc_abi.c) or the kernel side (phase 5c QEMU
 * smoke); it locks the program logic only.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capylibc/capylibc.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                    \
    do {                                                              \
        tests_run++;                                                  \
        printf("  %-58s ", name);                                     \
    } while (0)
#define PASS()                                                        \
    do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) printf("FAIL: %s\n", msg)

/* ---- host stubs for capylibc -------------------------------------- */

static int g_write_calls = 0;
static int g_last_fd = -1;
static char g_last_buf[64];
static size_t g_last_len = 0;
static int g_exit_calls = 0;
static int g_other_syscalls = 0;

void capy_exit(int status) {
    (void)status;
    g_exit_calls++;
    fprintf(stderr, "unexpected direct capy_exit call from hello\n");
    abort();
}
long capy_write(int fd, const void *buf, size_t len) {
    g_write_calls++;
    g_last_fd = fd;
    if (len < sizeof(g_last_buf)) {
        memcpy(g_last_buf, buf, len);
        g_last_buf[len] = '\0';
    } else {
        memcpy(g_last_buf, buf, sizeof(g_last_buf) - 1);
        g_last_buf[sizeof(g_last_buf) - 1] = '\0';
    }
    g_last_len = len;
    return (long)len;
}
long capy_read(int fd, void *buf, size_t len) {
    (void)fd; (void)buf; (void)len; g_other_syscalls++; return 0;
}
int capy_getpid(void)  { g_other_syscalls++; return 0; }
int capy_getppid(void) { g_other_syscalls++; return 0; }
void capy_yield(void)  { g_other_syscalls++; }
void capy_sleep(unsigned long t) { (void)t; g_other_syscalls++; }
long capy_time(void)   { g_other_syscalls++; return 0; }

/* ---- hello/main.c under a renamed entry point -------------------- */

#define main hello_main
#include "../userland/bin/hello/main.c"
#undef main

/* ---- the actual checks ------------------------------------------- */

int test_hello_program_run(void) {
    printf("[test_hello_program]\n");
    tests_run = 0;
    tests_passed = 0;
    g_write_calls = 0;
    g_last_fd = -999;
    memset(g_last_buf, 0, sizeof(g_last_buf));
    g_last_len = 0;
    g_exit_calls = 0;
    g_other_syscalls = 0;

    int rc = hello_main();

    TEST("main returned 0 (crt0 forwards as SYS_EXIT code)");
    if (rc == 0) PASS();
    else FAIL("hello main returned non-zero");

    TEST("exactly one capy_write call");
    if (g_write_calls == 1) PASS();
    else FAIL("expected 1 capy_write call");

    TEST("capy_write fd == 1 (stdout)");
    if (g_last_fd == 1) PASS();
    else FAIL("hello wrote to wrong fd");

    TEST("capy_write content == \"hello, capyland\\n\"");
    if (strcmp(g_last_buf, "hello, capyland\n") == 0) PASS();
    else FAIL("hello message drift");

    TEST("capy_write length == 16 (no NUL terminator)");
    if (g_last_len == 16) PASS();
    else FAIL("hello message length drift");

    TEST("hello does not call capy_exit directly (crt0 owns SYS_EXIT)");
    if (g_exit_calls == 0) PASS();
    else FAIL("hello called capy_exit; should let crt0 do it");

    TEST("hello does not invoke other syscalls");
    if (g_other_syscalls == 0) PASS();
    else FAIL("hello touched a syscall it should not");

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
