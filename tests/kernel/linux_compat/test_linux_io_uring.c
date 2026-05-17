#include "kernel/linux_compat/linux_io_uring.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    int dummy;
    TEST("io_uring_setup entries=0 -> -EINVAL");
    if (linux_io_uring_setup(0, &dummy) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t2(void) {
    int dummy;
    TEST("io_uring_setup entries > MAX -> -EINVAL");
    if (linux_io_uring_setup(LINUX_IORING_MAX_ENTRIES + 1, &dummy)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    int dummy;
    TEST("io_uring_setup non-power-of-2 -> -EINVAL");
    if (linux_io_uring_setup(7, &dummy) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("io_uring_setup NULL params -> -EFAULT");
    if (linux_io_uring_setup(64, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t5(void) {
    int dummy;
    TEST("io_uring_setup well-formed -> -ENOSYS (Marco M1)");
    if (linux_io_uring_setup(64, &dummy) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t6(void) {
    int dummy;
    TEST("io_uring_setup MAX entries (4096) -> -ENOSYS");
    if (linux_io_uring_setup(LINUX_IORING_MAX_ENTRIES, &dummy)
        == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t7(void) {
    TEST("io_uring_enter fd<0 -> -EBADF");
    if (linux_io_uring_enter(-1, 0, 0, 0, NULL, 0) == -LINUX_EBADF)
        PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("io_uring_enter unknown flags -> -EINVAL");
    if (linux_io_uring_enter(7, 0, 0, 0xDEAD, NULL, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    int dummy;
    TEST("io_uring_enter sig with sigsz != 8 -> -EINVAL");
    if (linux_io_uring_enter(7, 0, 0, 0, &dummy, 7)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    TEST("io_uring_enter NULL sig sigsz=0 -> -ENOSYS");
    if (linux_io_uring_enter(7, 0, 0,
        LINUX_IORING_ENTER_GETEVENTS, NULL, 0) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t11(void) {
    int dummy;
    TEST("io_uring_enter sig sigsz=8 -> -ENOSYS (well-formed)");
    if (linux_io_uring_enter(7, 0, 0, 0, &dummy, 8)
        == -LINUX_ENOSYS) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("io_uring_register fd<0 -> -EBADF");
    if (linux_io_uring_register(-1, LINUX_IORING_REGISTER_BUFFERS,
                                NULL, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("io_uring_register opcode out of range -> -EINVAL");
    if (linux_io_uring_register(7, LINUX_IORING_REGISTER_OPCODE_MAX,
                                NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    TEST("io_uring_register nr_args>0 NULL arg -> -EFAULT");
    if (linux_io_uring_register(7, LINUX_IORING_REGISTER_BUFFERS,
                                NULL, 1) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t15(void) {
    int dummy;
    TEST("io_uring_register well-formed -> -ENOSYS");
    if (linux_io_uring_register(7, LINUX_IORING_REGISTER_BUFFERS,
                                &dummy, 1) == -LINUX_ENOSYS) PASS();
    else FAIL("");
}

int test_linux_io_uring_run(void) {
    printf("[test_linux_io_uring]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
