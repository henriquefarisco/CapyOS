#include "kernel/linux_compat/linux_mincore.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    uint8_t v[4];
    TEST("mincore unaligned addr -> -EINVAL");
    if (linux_mincore(0x123, 4096, v) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    uint8_t v[4];
    TEST("mincore length=0 -> 0 (no-op)");
    if (linux_mincore(0x1000, 0, v) == 0) PASS();
    else FAIL("");
}
static void t3(void) {
    TEST("mincore length>0 NULL vec -> -EFAULT");
    if (linux_mincore(0x1000, 4096, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    uint8_t v[1] = {0x99};
    int64_t r = linux_mincore(0x1000, 4096, v);
    TEST("mincore single page -> 1 byte set to 1");
    if (r == 0 && v[0] == 1) PASS();
    else FAIL("");
}
static void t5(void) {
    uint8_t v[3] = {0xFF, 0xFF, 0xFF};
    /* 4097 bytes -> 2 pages worth of bits. */
    int64_t r = linux_mincore(0x1000, 4097, v);
    TEST("mincore 1 byte over page boundary -> 2 bytes set");
    if (r == 0 && v[0] == 1 && v[1] == 1 && v[2] == 0xFF /* untouched */)
        PASS();
    else FAIL("");
}
static void t6(void) {
    uint8_t v[10];
    memset(v, 0x42, sizeof(v));
    /* 8 pages worth: 32 KiB == 8 * 4096 = 32768. */
    int64_t r = linux_mincore(0x1000, 32768, v);
    TEST("mincore 8 pages -> bytes 0..7 set; 8..9 untouched");
    int ok = (r == 0);
    for (int i = 0; i < 8; i++) if (v[i] != 1) ok = 0;
    for (int i = 8; i < 10; i++) if (v[i] != 0x42) ok = 0;
    if (ok) PASS();
    else FAIL("");
}
static void t7(void) {
    uint8_t v[1];
    /* Overflow: addr near uintptr_t max + length wraps. */
    uintptr_t addr = (uintptr_t)-(intptr_t)0x10000;
    /* addr is page-aligned (high bits all set, lower 12 bits zero
     * if addr is multiple of 0x1000). 0x10000 = 16 * 4096 so OK. */
    /* Now adding 0x20000 to addr wraps. */
    TEST("mincore addr+length overflow -> -ENOMEM");
    if (linux_mincore(addr, 0x20000, v) == -LINUX_ENOMEM) PASS();
    else FAIL("");
}
static void t8(void) {
    uint8_t v[2];
    /* 1 byte -> 1 page. */
    int64_t r = linux_mincore(0x4000, 1, v);
    TEST("mincore length<page rounds up to 1 byte");
    if (r == 0 && v[0] == 1) PASS();
    else FAIL("");
}

int test_linux_mincore_run(void) {
    printf("[test_linux_mincore]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
