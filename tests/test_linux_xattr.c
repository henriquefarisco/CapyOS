#include "kernel/linux_compat/linux_xattr.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    TEST("setxattr(NULL, ...) -> -EFAULT");
    if (linux_setxattr(NULL, "user.x", "v", 1, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    TEST("setxattr(\"\", ...) -> -ENOENT");
    if (linux_setxattr("", "user.x", "v", 1, 0) == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    TEST("setxattr(path, NULL, ...) -> -EFAULT");
    if (linux_setxattr("/x", NULL, "v", 1, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("setxattr(path, \"\", ...) -> -ERANGE (empty xattr name)");
    if (linux_setxattr("/x", "", "v", 1, 0) == -LINUX_ERANGE) PASS();
    else FAIL("");
}
static void t5(void) {
    char big[260];
    for (int i = 0; i < 259; i++) big[i] = 'a';
    big[259] = '\0';
    TEST("setxattr name too long -> -ENAMETOOLONG");
    if (linux_setxattr("/x", big, "v", 1, 0) == -LINUX_ENAMETOOLONG) PASS();
    else FAIL("");
}
static void t6(void) {
    TEST("setxattr size > XATTR_SIZE_MAX -> -E2BIG");
    if (linux_setxattr("/x", "user.x", "v", LINUX_XATTR_SIZE_MAX + 1, 0)
        == -LINUX_E2BIG) PASS();
    else FAIL("");
}
static void t7(void) {
    TEST("setxattr unknown flag -> -EINVAL");
    if (linux_setxattr("/x", "user.x", "v", 1, 0xDEAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("setxattr CREATE flag -> -EOPNOTSUPP (no xattr storage)");
    if (linux_setxattr("/x", "user.x", "v", 1, LINUX_XATTR_CREATE)
        == -LINUX_EOPNOTSUPP) PASS();
    else FAIL("");
}
static void t9(void) {
    TEST("fsetxattr(-1, ...) -> -EBADF");
    if (linux_fsetxattr(-1, "user.x", "v", 1, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t10(void) {
    TEST("setxattr size > 0 with NULL value -> -EFAULT");
    if (linux_setxattr("/x", "user.x", NULL, 1, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("getxattr missing attribute -> -ENODATA");
    char buf[16];
    if (linux_getxattr("/x", "user.missing", buf, sizeof(buf))
        == -LINUX_ENODATA) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("getxattr size>0 with NULL value -> -EFAULT");
    if (linux_getxattr("/x", "user.x", NULL, 16) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("getxattr size=0 (probe) -> -ENODATA (no attr exists)");
    if (linux_getxattr("/x", "user.x", NULL, 0) == -LINUX_ENODATA) PASS();
    else FAIL("");
}
static void t14(void) {
    TEST("fgetxattr(-1, ...) -> -EBADF");
    if (linux_fgetxattr(-1, "user.x", NULL, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t15(void) {
    char list[64];
    int64_t r = linux_listxattr("/x", list, sizeof(list));
    TEST("listxattr -> 0 bytes (no attributes)");
    if (r == 0) PASS();
    else FAIL("");
}
static void t16(void) {
    TEST("listxattr size=0 (probe) -> 0");
    if (linux_listxattr("/x", NULL, 0) == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    TEST("listxattr size>0 with NULL list -> -EFAULT");
    if (linux_listxattr("/x", NULL, 16) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t18(void) {
    TEST("flistxattr(-1) -> -EBADF");
    if (linux_flistxattr(-1, NULL, 0) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t19(void) {
    TEST("removexattr missing -> -ENODATA");
    if (linux_removexattr("/x", "user.missing") == -LINUX_ENODATA) PASS();
    else FAIL("");
}
static void t20(void) {
    TEST("removexattr empty name -> -ERANGE");
    if (linux_removexattr("/x", "") == -LINUX_ERANGE) PASS();
    else FAIL("");
}
static void t21(void) {
    TEST("fremovexattr(-1, ...) -> -EBADF");
    if (linux_fremovexattr(-1, "user.x") == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t22(void) {
    TEST("lsetxattr -> -EOPNOTSUPP (l-form same fallback)");
    if (linux_lsetxattr("/x", "user.x", "v", 1, 0) == -LINUX_EOPNOTSUPP) PASS();
    else FAIL("");
}
static void t23(void) {
    TEST("lgetxattr -> -ENODATA (l-form same fallback)");
    if (linux_lgetxattr("/x", "user.x", NULL, 0) == -LINUX_ENODATA) PASS();
    else FAIL("");
}

int test_linux_xattr_run(void) {
    printf("[test_linux_xattr]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16();
    t17(); t18(); t19(); t20(); t21(); t22(); t23();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
