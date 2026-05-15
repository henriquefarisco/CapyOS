#include "kernel/linux_compat/linux_landlock.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset(VERSION) -> ABI version");
    int64_t r = linux_landlock_create_ruleset(NULL, 0,
        LINUX_LANDLOCK_CREATE_RULESET_VERSION);
    if (r >= 1 && r <= 16) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset(VERSION + attr non-NULL) -> -EINVAL");
    struct linux_landlock_ruleset_attr a = {0,0,0};
    int64_t r = linux_landlock_create_ruleset(&a, 0,
        LINUX_LANDLOCK_CREATE_RULESET_VERSION);
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset(VERSION | unknown) -> -EINVAL");
    int64_t r = linux_landlock_create_ruleset(NULL, 0,
        LINUX_LANDLOCK_CREATE_RULESET_VERSION | 0x80);
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset NULL attr -> -EFAULT");
    int64_t r = linux_landlock_create_ruleset(NULL, 16, 0);
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset size<min -> -EINVAL");
    struct linux_landlock_ruleset_attr a = {0,0,0};
    if (linux_landlock_create_ruleset(&a, 8, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset unknown handled_access_fs -> -EINVAL");
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = 1ULL << 30
    };
    if (linux_landlock_create_ruleset(&a, 16, 0) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t7(void) {
    linux_landlock_reset_for_tests();
    TEST("create_ruleset all-zero access -> -ENOMSG");
    struct linux_landlock_ruleset_attr a = {0,0,0};
    if (linux_landlock_create_ruleset(&a, 16, 0) == -LINUX_ENOMSG)
        PASS();
    else FAIL("");
}
static void t8(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("create_ruleset valid -> fd >= LINUX_LANDLOCK_FD_BASE");
    if (fd == LINUX_LANDLOCK_FD_BASE) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_landlock_reset_for_tests();
    /* Exhaust ruleset table. */
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t last = -1;
    for (int i = 0; i < LINUX_LANDLOCK_RULESET_FD_MAX; i++) {
        last = linux_landlock_create_ruleset(&a, 16, 0);
    }
    int64_t r = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("create_ruleset table exhausted -> -ENFILE");
    if (last == LINUX_LANDLOCK_FD_BASE +
                LINUX_LANDLOCK_RULESET_FD_MAX - 1 &&
        r == -LINUX_ENFILE) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_landlock_reset_for_tests();
    TEST("add_rule invalid fd -> -EBADF");
    int dummy = 0;
    if (linux_landlock_add_rule(99, LINUX_LANDLOCK_RULE_PATH_BENEATH,
                                &dummy, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("add_rule unknown rule_type -> -EINVAL");
    int dummy = 0;
    if (linux_landlock_add_rule((int)fd, 99, &dummy, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("add_rule NULL rule_attr -> -EFAULT");
    if (linux_landlock_add_rule((int)fd,
                                LINUX_LANDLOCK_RULE_PATH_BENEATH,
                                NULL, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t13(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    int dummy = 0;
    int64_t r = linux_landlock_add_rule((int)fd,
                                        LINUX_LANDLOCK_RULE_PATH_BENEATH,
                                        &dummy, 0);
    TEST("add_rule valid -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    linux_landlock_reset_for_tests();
    TEST("restrict_self invalid fd -> -EBADF");
    if (linux_landlock_restrict_self(99, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t15(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("restrict_self valid fd -> 0");
    if (linux_landlock_restrict_self((int)fd, 0) == 0) PASS();
    else FAIL("");
}
static void t16(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("restrict_self flags!=0 -> -EINVAL");
    if (linux_landlock_restrict_self((int)fd, 1) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t17(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int64_t fd = linux_landlock_create_ruleset(&a, 16, 0);
    int64_t c = linux_landlock_close((int)fd);
    int64_t again = linux_landlock_create_ruleset(&a, 16, 0);
    TEST("landlock close releases ruleset fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("");
}
static void t18(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int fd = (int)linux_landlock_create_ruleset(&a, 16, 0);
    uint8_t buf[8];
    TEST("landlock ruleset read -> -EINVAL");
    if (linux_landlock_read(fd, buf, sizeof(buf)) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t19(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int fd = (int)linux_landlock_create_ruleset(&a, 16, 0);
    TEST("landlock ruleset write -> -EINVAL");
    if (linux_landlock_write(fd, "x", 1) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t20(void) {
    linux_landlock_reset_for_tests();
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    int fd = (int)linux_landlock_create_ruleset(&a, 16, 0);
    TEST("landlock ruleset lseek -> -ESPIPE");
    if (linux_landlock_lseek(fd, 0, 0) == -LINUX_ESPIPE) PASS();
    else FAIL("");
}
static void t21(void) {
    linux_landlock_reset_for_tests();
    uint8_t buf[8];
    TEST("landlock ruleset read on unknown fd -> -EBADF");
    if (linux_landlock_read(LINUX_LANDLOCK_FD_BASE, buf, sizeof(buf))
        == -LINUX_EBADF) PASS();
    else FAIL("");
}

int test_linux_landlock_run(void) {
    printf("[test_linux_landlock]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16();
    t17(); t18(); t19(); t20(); t21();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
