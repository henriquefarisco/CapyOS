#include "kernel/linux_compat/linux_namespace.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t1(void) {
    TEST("unshare(0) -> 0 (no-op)");
    if (linux_unshare(0) == 0) PASS();
    else FAIL("");
}
static void t2(void) {
    TEST("unshare(CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWIPC) -> 0");
    if (linux_unshare(LINUX_CLONE_NEWUSER | LINUX_CLONE_NEWNET |
                      LINUX_CLONE_NEWIPC) == 0) PASS();
    else FAIL("");
}
static void t3(void) {
    TEST("unshare unknown bits -> -EINVAL");
    if (linux_unshare(0x80000000) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    TEST("unshare(THREAD without VM+SIGHAND) -> -EINVAL");
    if (linux_unshare(LINUX_CLONE_THREAD) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    TEST("unshare(SIGHAND without VM) -> -EINVAL");
    if (linux_unshare(LINUX_CLONE_SIGHAND) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t6(void) {
    TEST("unshare(THREAD|VM|SIGHAND) -> 0");
    if (linux_unshare(LINUX_CLONE_THREAD | LINUX_CLONE_VM |
                      LINUX_CLONE_SIGHAND) == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    TEST("mount(NULL target) -> -EFAULT");
    if (linux_mount("none", NULL, "tmpfs", 0, NULL) == -LINUX_EFAULT)
        PASS();
    else FAIL("");
}
static void t8(void) {
    TEST("mount(empty target) -> -ENOENT");
    if (linux_mount("none", "", "tmpfs", 0, NULL) == -LINUX_ENOENT)
        PASS();
    else FAIL("");
}
static void t9(void) {
    TEST("mount unknown flag bit -> -EINVAL");
    if (linux_mount("none", "/tmp", "tmpfs", 0x80000000ULL, NULL)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t10(void) {
    TEST("mount NULL fstype (non-bind/move/remount) -> -EFAULT");
    if (linux_mount("none", "/tmp", NULL, 0, NULL) == -LINUX_EFAULT)
        PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("mount unknown fstype -> -ENODEV");
    if (linux_mount("none", "/tmp", "btrfs", 0, NULL) == -LINUX_ENODEV)
        PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("mount(tmpfs, /tmp) -> 0");
    if (linux_mount("none", "/tmp", "tmpfs", 0, NULL) == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("mount(proc, /proc) -> 0");
    if (linux_mount("none", "/proc", "proc", 0, NULL) == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    TEST("mount BIND with NULL source -> -EFAULT");
    if (linux_mount(NULL, "/dest", NULL, LINUX_MS_BIND, NULL)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t15(void) {
    TEST("mount BIND with valid source -> 0 (skips fstype lookup)");
    if (linux_mount("/src", "/dest", NULL, LINUX_MS_BIND, NULL) == 0)
        PASS();
    else FAIL("");
}
static void t16(void) {
    TEST("mount REMOUNT no source/fstype -> 0");
    if (linux_mount(NULL, "/old", NULL, LINUX_MS_REMOUNT, NULL) == 0)
        PASS();
    else FAIL("");
}
static void t17(void) {
    TEST("umount2(NULL) -> -EFAULT");
    if (linux_umount2(NULL, 0) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t18(void) {
    TEST("umount2(\"\") -> -ENOENT");
    if (linux_umount2("", 0) == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t19(void) {
    TEST("umount2 unknown flags -> -EINVAL");
    if (linux_umount2("/tmp", 0x80) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t20(void) {
    TEST("umount2(MNT_DETACH | UMOUNT_NOFOLLOW) -> 0");
    if (linux_umount2("/tmp", LINUX_MNT_DETACH | LINUX_UMOUNT_NOFOLLOW)
        == 0) PASS();
    else FAIL("");
}

int test_linux_namespace_run(void) {
    printf("[test_linux_namespace]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10();
    t11(); t12(); t13(); t14(); t15(); t16(); t17(); t18(); t19(); t20();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
