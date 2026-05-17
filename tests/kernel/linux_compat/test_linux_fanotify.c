#include "kernel/linux_compat/linux_fanotify.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_init(0, 0) -> first fd in FAN range");
    if (fd == LINUX_FAN_FD_BASE) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_fanotify_reset_for_tests();
    TEST("fanotify_init unknown flags -> -EINVAL");
    if (linux_fanotify_init(0xDEADBE, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(LINUX_FAN_CLOEXEC |
                                     LINUX_FAN_CLASS_CONTENT |
                                     LINUX_FAN_REPORT_FID, 0);
    TEST("fanotify_init known flags -> ok");
    if (fd == LINUX_FAN_FD_BASE) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_fanotify_reset_for_tests();
    int64_t a = linux_fanotify_init(0, 0);
    int64_t b = linux_fanotify_init(0, 0);
    TEST("fanotify_init returns distinct fds");
    if (a == LINUX_FAN_FD_BASE && b == LINUX_FAN_FD_BASE + 1) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_fanotify_reset_for_tests();
    int64_t last = -1;
    for (int i = 0; i < LINUX_FAN_FD_MAX; i++) {
        last = linux_fanotify_init(0, 0);
    }
    int64_t r = linux_fanotify_init(0, 0);
    TEST("fanotify_init exhaustion -> -ENFILE");
    if (last == LINUX_FAN_FD_BASE + LINUX_FAN_FD_MAX - 1 &&
        r == -LINUX_ENFILE) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_fanotify_reset_for_tests();
    TEST("fanotify_mark invalid fd -> -EBADF");
    if (linux_fanotify_mark(99, LINUX_FAN_MARK_ADD, 0, 0, "/x")
        == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark unknown flag bit -> -EINVAL");
    if (linux_fanotify_mark((int)fd, 0xDEAD, 0, 0, "/x")
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark ADD | REMOVE -> -EINVAL (mutex)");
    if (linux_fanotify_mark((int)fd,
        LINUX_FAN_MARK_ADD | LINUX_FAN_MARK_REMOVE, 0, 0, "/x")
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark FLUSH | ADD -> -EINVAL");
    if (linux_fanotify_mark((int)fd,
        LINUX_FAN_MARK_FLUSH | LINUX_FAN_MARK_ADD, 0, 0, "/x")
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark sem ADD/REMOVE/FLUSH -> -EINVAL");
    if (linux_fanotify_mark((int)fd, 0, 0, 0, "/x")
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark ADD with NULL pathname -> -EFAULT");
    if (linux_fanotify_mark((int)fd, LINUX_FAN_MARK_ADD, 0, 0, NULL)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark FLUSH with NULL pathname -> 0 (FLUSH ignores path)");
    if (linux_fanotify_mark((int)fd, LINUX_FAN_MARK_FLUSH, 0, 0, NULL)
        == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark ADD valid -> 0");
    if (linux_fanotify_mark((int)fd, LINUX_FAN_MARK_ADD, 0, 0, "/x")
        == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify_mark REMOVE valid -> 0");
    if (linux_fanotify_mark((int)fd, LINUX_FAN_MARK_REMOVE, 0, 0, "/x")
        == 0) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    int64_t c = linux_fanotify_close((int)fd);
    int64_t again = linux_fanotify_init(0, 0);
    TEST("fanotify close releases fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    uint8_t buf[8];
    TEST("fanotify read on live fd -> -EAGAIN until events land");
    if (linux_fanotify_read((int)fd, buf, sizeof(buf)) == -LINUX_EAGAIN)
        PASS();
    else FAIL("");
}
static void t17(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify write on live fd -> -EINVAL");
    if (linux_fanotify_write((int)fd, "x", 1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t18(void) {
    linux_fanotify_reset_for_tests();
    int64_t fd = linux_fanotify_init(0, 0);
    TEST("fanotify lseek on live fd -> -ESPIPE");
    if (linux_fanotify_lseek((int)fd, 0, 0) == -LINUX_ESPIPE) PASS();
    else FAIL("");
}
static void t19(void) {
    linux_fanotify_reset_for_tests();
    uint8_t buf[8];
    TEST("fanotify read on unknown fd -> -EBADF");
    if (linux_fanotify_read(LINUX_FAN_FD_BASE, buf, sizeof(buf))
        == -LINUX_EBADF) PASS();
    else FAIL("");
}

int test_linux_fanotify_run(void) {
    printf("[test_linux_fanotify]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14();
    t15(); t16(); t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
