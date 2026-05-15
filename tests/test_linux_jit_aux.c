#include "kernel/linux_compat/linux_jit_aux.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_jit_aux_reset_for_tests();
    int64_t r = linux_membarrier(LINUX_MEMBARRIER_CMD_QUERY, 0, 0);
    TEST("membarrier(QUERY) -> supported bitmask");
    if (r == LINUX_MEMBARRIER_SUPPORTED) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_jit_aux_reset_for_tests();
    TEST("membarrier(QUERY) with flags -> -EINVAL");
    if (linux_membarrier(LINUX_MEMBARRIER_CMD_QUERY, 1, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_jit_aux_reset_for_tests();
    TEST("membarrier unknown flag -> -EINVAL");
    if (linux_membarrier(
        LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0xDEAD, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_jit_aux_reset_for_tests();
    TEST("membarrier PRIVATE_EXPEDITED without REGISTER -> -EPERM");
    if (linux_membarrier(LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0)
        == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_jit_aux_reset_for_tests();
    int64_t r1 = linux_membarrier(
        LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0);
    int64_t r2 = linux_membarrier(
        LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0);
    TEST("membarrier REGISTER then PRIVATE_EXPEDITED -> 0");
    if (r1 == 0 && r2 == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_jit_aux_reset_for_tests();
    int64_t r = linux_membarrier(LINUX_MEMBARRIER_CMD_GLOBAL, 0, 0);
    TEST("membarrier GLOBAL -> 0 (no register required)");
    if (r == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_jit_aux_reset_for_tests();
    TEST("membarrier unknown cmd -> -EINVAL");
    if (linux_membarrier(99, 0, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_jit_aux_reset_for_tests();
    int64_t fd = linux_userfaultfd(0);
    TEST("userfaultfd(0) -> first fd in UFFD range");
    if (fd == LINUX_UFFD_FD_BASE) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_jit_aux_reset_for_tests();
    TEST("userfaultfd unknown flags -> -EINVAL");
    if (linux_userfaultfd(0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_jit_aux_reset_for_tests();
    int64_t fd1 = linux_userfaultfd(LINUX_UFFD_CLOEXEC);
    int64_t fd2 = linux_userfaultfd(LINUX_UFFD_NONBLOCK);
    TEST("userfaultfd allocates distinct fds");
    if (fd1 == LINUX_UFFD_FD_BASE && fd2 == LINUX_UFFD_FD_BASE + 1)
        PASS();
    else FAIL("");
}
static void t11(void) {
    linux_jit_aux_reset_for_tests();
    /* Exhaust the table. */
    int64_t last_fd = -1;
    for (int i = 0; i < LINUX_UFFD_FD_MAX; i++) {
        last_fd = linux_userfaultfd(0);
    }
    int64_t r = linux_userfaultfd(0);
    TEST("userfaultfd table exhausted -> -ENFILE");
    if (last_fd == LINUX_UFFD_FD_BASE + LINUX_UFFD_FD_MAX - 1 &&
        r == -LINUX_ENFILE) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("sched_rr_get_interval pid<0 -> -EINVAL");
    struct linux_jit_timespec tp;
    if (linux_sched_rr_get_interval(-1, &tp) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("sched_rr_get_interval NULL tp -> -EFAULT");
    if (linux_sched_rr_get_interval(0, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t14(void) {
    struct linux_jit_timespec tp = {-1, -1};
    int64_t r = linux_sched_rr_get_interval(0, &tp);
    TEST("sched_rr_get_interval(0) -> 100ms (default RR slice)");
    if (r == 0 && tp.tv_sec == 0 && tp.tv_nsec == 100000000) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_jit_aux_reset_for_tests();
    int64_t fd = linux_userfaultfd(0);
    int64_t c = linux_userfaultfd_close((int)fd);
    int64_t again = linux_userfaultfd(0);
    TEST("userfaultfd close releases fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_jit_aux_reset_for_tests();
    int fd = (int)linux_userfaultfd(0);
    uint8_t buf[8];
    TEST("userfaultfd read on live fd -> -EAGAIN until events land");
    if (linux_userfaultfd_read(fd, buf, sizeof(buf)) == -LINUX_EAGAIN) PASS();
    else FAIL("");
}
static void t17(void) {
    linux_jit_aux_reset_for_tests();
    int fd = (int)linux_userfaultfd(0);
    TEST("userfaultfd write on live fd -> -EINVAL");
    if (linux_userfaultfd_write(fd, "x", 1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t18(void) {
    linux_jit_aux_reset_for_tests();
    int fd = (int)linux_userfaultfd(0);
    TEST("userfaultfd lseek on live fd -> -ESPIPE");
    if (linux_userfaultfd_lseek(fd, 0, 0) == -LINUX_ESPIPE) PASS();
    else FAIL("");
}
static void t19(void) {
    linux_jit_aux_reset_for_tests();
    uint8_t buf[8];
    TEST("userfaultfd read on unknown fd -> -EBADF");
    if (linux_userfaultfd_read(LINUX_UFFD_FD_BASE, buf, sizeof(buf))
        == -LINUX_EBADF) PASS();
    else FAIL("");
}

int test_linux_jit_aux_run(void) {
    printf("[test_linux_jit_aux]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14();
    t15(); t16(); t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
