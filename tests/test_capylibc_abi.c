/*
 * Tests for the capylibc <-> kernel syscall ABI (M4 phase 5a).
 *
 * Locks two contracts:
 *
 *   1. The syscall numbers in include/kernel/syscall_numbers.h have
 *      not silently drifted. capylibc's asm stubs
 *      (userland/lib/capylibc/syscall_stubs.S) reference these by
 *      symbolic name; if any number changes, every shipped user
 *      binary breaks because its already-resolved `mov $N, %rax`
 *      points at the wrong handler.
 *
 *   2. include/kernel/syscall.h still re-exports every SYS_* and
 *      SYSCALL_COUNT via the new asm-friendly numbers header, so the
 *      kernel TUs that include `kernel/syscall.h` keep compiling
 *      after the split.
 *
 * The struct syscall_frame layout is already locked indirectly by
 * tests/test_syscall_msr.c (selectors and STAR layout) and by
 * src/arch/x86_64/syscall/syscall_entry.S (push/pop ordering); we
 * also pin the on-stack offset of every register here so the
 * kernel's sys_* handlers and the asm entry stay in lock-step.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/syscall.h"           /* re-exports SYS_*, SYSCALL_COUNT */
#include "kernel/syscall_numbers.h"   /* canonical source of the numbers */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do {                                                                   \
        printf("OK\n");                                                    \
        tests_passed++;                                                    \
    } while (0)
#define FAIL(msg)                                                          \
    do {                                                                   \
        printf("FAIL: %s\n", msg);                                         \
    } while (0)

/* ---- 1. Syscall numbers have not drifted ------------------------- */

static void test_syscall_numbers(void) {
    /* The list mirrors the numeric column of syscall_numbers.h. If
     * a kernel maintainer renumbers a syscall the test fails before
     * any user binary is rebuilt against a bad value. */
    struct {
        const char *label;
        int actual;
        int expected;
    } const table[] = {
        { "SYS_EXIT == 0",     SYS_EXIT,     0 },
        { "SYS_READ == 1",     SYS_READ,     1 },
        { "SYS_WRITE == 2",    SYS_WRITE,    2 },
        { "SYS_OPEN == 3",     SYS_OPEN,     3 },
        { "SYS_CLOSE == 4",    SYS_CLOSE,    4 },
        { "SYS_STAT == 5",     SYS_STAT,     5 },
        { "SYS_FSTAT == 6",    SYS_FSTAT,    6 },
        { "SYS_LSEEK == 7",    SYS_LSEEK,    7 },
        { "SYS_MMAP == 8",     SYS_MMAP,     8 },
        { "SYS_MUNMAP == 9",   SYS_MUNMAP,   9 },
        { "SYS_BRK == 10",     SYS_BRK,      10 },
        { "SYS_FORK == 11",    SYS_FORK,     11 },
        { "SYS_EXEC == 12",    SYS_EXEC,     12 },
        { "SYS_WAIT == 13",    SYS_WAIT,     13 },
        { "SYS_GETPID == 14",  SYS_GETPID,   14 },
        { "SYS_GETPPID == 15", SYS_GETPPID,  15 },
        { "SYS_KILL == 16",    SYS_KILL,     16 },
        { "SYS_YIELD == 17",   SYS_YIELD,    17 },
        { "SYS_SLEEP == 18",   SYS_SLEEP,    18 },
        { "SYS_DUP == 19",     SYS_DUP,      19 },
        { "SYS_DUP2 == 20",    SYS_DUP2,     20 },
        { "SYS_PIPE == 21",    SYS_PIPE,     21 },
        { "SYS_MKDIR == 22",   SYS_MKDIR,    22 },
        { "SYS_RMDIR == 23",   SYS_RMDIR,    23 },
        { "SYS_UNLINK == 24",  SYS_UNLINK,   24 },
        { "SYS_RENAME == 25",  SYS_RENAME,   25 },
        { "SYS_GETCWD == 26",  SYS_GETCWD,   26 },
        { "SYS_CHDIR == 27",   SYS_CHDIR,    27 },
        { "SYS_SOCKET == 28",  SYS_SOCKET,   28 },
        { "SYS_BIND == 29",    SYS_BIND,     29 },
        { "SYS_LISTEN == 30",  SYS_LISTEN,   30 },
        { "SYS_ACCEPT == 31",  SYS_ACCEPT,   31 },
        { "SYS_CONNECT == 32", SYS_CONNECT,  32 },
        { "SYS_SEND == 33",    SYS_SEND,     33 },
        { "SYS_RECV == 34",    SYS_RECV,     34 },
        { "SYS_GETUID == 35",  SYS_GETUID,   35 },
        { "SYS_GETGID == 36",  SYS_GETGID,   36 },
        { "SYS_SETUID == 37",  SYS_SETUID,   37 },
        { "SYS_SETGID == 38",  SYS_SETGID,   38 },
        { "SYS_TIME == 39",    SYS_TIME,     39 },
        { "SYS_IOCTL == 40",   SYS_IOCTL,    40 },
    };

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        TEST(table[i].label);
        if (table[i].actual == table[i].expected) PASS();
        else FAIL("syscall number drift");
    }

    TEST("SYSCALL_COUNT == 41");
    if (SYSCALL_COUNT == 41) PASS();
    else FAIL("syscall table size drift");
}

/* ---- 2. struct syscall_frame layout (locked C/asm contract) ----- */

static void test_syscall_frame_layout(void) {
    /* The on-stack frame is built by syscall_entry.S with a fixed
     * order: rax, rdi, rsi, rdx, r10, r8, r9, rcx, r11, rip, rsp,
     * rflags. The sys_* handlers in src/kernel/syscall.c read each
     * field through the C struct, so any reordering would silently
     * mis-route arguments. Lock both offsets and total size. */
    TEST("offsetof(syscall_frame, rax)    == 0x00");
    if (offsetof(struct syscall_frame, rax)    == 0x00) PASS();
    else FAIL("rax offset drift");
    TEST("offsetof(syscall_frame, rdi)    == 0x08");
    if (offsetof(struct syscall_frame, rdi)    == 0x08) PASS();
    else FAIL("rdi offset drift");
    TEST("offsetof(syscall_frame, rsi)    == 0x10");
    if (offsetof(struct syscall_frame, rsi)    == 0x10) PASS();
    else FAIL("rsi offset drift");
    TEST("offsetof(syscall_frame, rdx)    == 0x18");
    if (offsetof(struct syscall_frame, rdx)    == 0x18) PASS();
    else FAIL("rdx offset drift");
    TEST("offsetof(syscall_frame, r10)    == 0x20");
    if (offsetof(struct syscall_frame, r10)    == 0x20) PASS();
    else FAIL("r10 offset drift");
    TEST("offsetof(syscall_frame, r8)     == 0x28");
    if (offsetof(struct syscall_frame, r8)     == 0x28) PASS();
    else FAIL("r8 offset drift");
    TEST("offsetof(syscall_frame, r9)     == 0x30");
    if (offsetof(struct syscall_frame, r9)     == 0x30) PASS();
    else FAIL("r9 offset drift");
    TEST("offsetof(syscall_frame, rcx)    == 0x38");
    if (offsetof(struct syscall_frame, rcx)    == 0x38) PASS();
    else FAIL("rcx offset drift");
    TEST("offsetof(syscall_frame, r11)    == 0x40");
    if (offsetof(struct syscall_frame, r11)    == 0x40) PASS();
    else FAIL("r11 offset drift");
    TEST("offsetof(syscall_frame, rip)    == 0x48");
    if (offsetof(struct syscall_frame, rip)    == 0x48) PASS();
    else FAIL("rip offset drift");
    TEST("offsetof(syscall_frame, rsp)    == 0x50");
    if (offsetof(struct syscall_frame, rsp)    == 0x50) PASS();
    else FAIL("rsp offset drift");
    TEST("offsetof(syscall_frame, rflags) == 0x58");
    if (offsetof(struct syscall_frame, rflags) == 0x58) PASS();
    else FAIL("rflags offset drift");

    TEST("sizeof(struct syscall_frame) == 0x60 (12 * 8)");
    if (sizeof(struct syscall_frame) == 0x60) PASS();
    else FAIL("syscall_frame size drift");
}

/* ---- 3. SysV register ABI documented in capylibc.h ---------------- */

static void test_register_abi(void) {
    /* This block is documentation that compiles. The register names
     * referenced by the SYSCALL ABI are not header constants we can
     * sizeof-check; what we lock here is the invariant that the
     * kernel's `struct syscall_frame` exposes the six argument
     * slots in the order capylibc.h advertises (rdi, rsi, rdx, r10,
     * r8, r9). The offsets above already do this; the asserts below
     * make the relationship explicit so a future refactor that
     * renames a field is forced to update both sides. */
    TEST("arg0 slot is %rdi");
    if (offsetof(struct syscall_frame, rdi) == 0x08) PASS();
    else FAIL("arg0 slot moved off %rdi");
    TEST("arg1 slot is %rsi");
    if (offsetof(struct syscall_frame, rsi) == 0x10) PASS();
    else FAIL("arg1 slot moved off %rsi");
    TEST("arg2 slot is %rdx");
    if (offsetof(struct syscall_frame, rdx) == 0x18) PASS();
    else FAIL("arg2 slot moved off %rdx");
    TEST("arg3 slot is %r10 (NOT %rcx; SYSCALL clobbers %rcx)");
    if (offsetof(struct syscall_frame, r10) == 0x20) PASS();
    else FAIL("arg3 slot moved off %r10");
    TEST("arg4 slot is %r8");
    if (offsetof(struct syscall_frame, r8) == 0x28) PASS();
    else FAIL("arg4 slot moved off %r8");
    TEST("arg5 slot is %r9");
    if (offsetof(struct syscall_frame, r9) == 0x30) PASS();
    else FAIL("arg5 slot moved off %r9");
}

int test_capylibc_abi_run(void) {
    printf("[test_capylibc_abi]\n");
    tests_run = 0;
    tests_passed = 0;
    test_syscall_numbers();
    test_syscall_frame_layout();
    test_register_abi();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
