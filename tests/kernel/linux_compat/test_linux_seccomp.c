#include "kernel/linux_compat/linux_seccomp.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_filter_calls;
static uint32_t g_last_flags;
static int64_t g_filter_rc;

static int64_t fake_filter(uint32_t flags, const void *prog, size_t len) {
    (void)prog; (void)len;
    g_filter_calls++; g_last_flags = flags;
    return g_filter_rc;
}

static void install_fake(int64_t rc) {
    static struct linux_seccomp_ops o;
    o.install_filter = fake_filter;
    g_filter_calls = 0;
    g_last_flags = 0;
    g_filter_rc = rc;
    linux_seccomp_reset_for_tests();
    linux_seccomp_install_ops(&o);
}

static void t1(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(STRICT, 0, NULL) -> 0");
    if (linux_seccomp(LINUX_SECCOMP_SET_MODE_STRICT, 0, NULL) == 0)
        PASS();
    else FAIL("");
}
static void t2(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(STRICT, flags, NULL) -> -EINVAL");
    if (linux_seccomp(LINUX_SECCOMP_SET_MODE_STRICT, 1, NULL)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(STRICT, 0, args!=NULL) -> -EINVAL");
    int dummy;
    if (linux_seccomp(LINUX_SECCOMP_SET_MODE_STRICT, 0, &dummy)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(FILTER, unknown flag) -> -EINVAL");
    int dummy;
    if (linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER, 0xDEAD, &dummy)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(FILTER, NULL args) -> -EFAULT");
    if (linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER, 0, NULL)
        == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_seccomp_reset_for_tests();
    int dummy;
    int64_t r = linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER,
                              LINUX_SECCOMP_FILTER_FLAG_TSYNC, &dummy);
    TEST("seccomp(FILTER, TSYNC) without ops -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake(0);
    int dummy;
    int64_t r = linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER,
                              LINUX_SECCOMP_FILTER_FLAG_TSYNC, &dummy);
    TEST("seccomp(FILTER) delegates to install_filter");
    if (r == 0 && g_filter_calls == 1 &&
        g_last_flags == LINUX_SECCOMP_FILTER_FLAG_TSYNC) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_seccomp_reset_for_tests();
    uint32_t a = LINUX_SECCOMP_RET_ALLOW;
    int64_t r = linux_seccomp(LINUX_SECCOMP_GET_ACTION_AVAIL, 0, &a);
    TEST("seccomp(GET_ACTION_AVAIL, ALLOW) -> 0");
    if (r == 0) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_seccomp_reset_for_tests();
    uint32_t a = 0xCAFEBABE;
    int64_t r = linux_seccomp(LINUX_SECCOMP_GET_ACTION_AVAIL, 0, &a);
    TEST("seccomp(GET_ACTION_AVAIL, unknown) -> -EOPNOTSUPP");
    if (r == -LINUX_EOPNOTSUPP) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_seccomp_reset_for_tests();
    uint16_t sizes[3] = {0, 0, 0};
    int64_t r = linux_seccomp(LINUX_SECCOMP_GET_NOTIF_SIZES, 0, sizes);
    TEST("seccomp(GET_NOTIF_SIZES) writes notif/resp/data sizes");
    if (r == 0 && sizes[0] == 80 && sizes[1] == 24 && sizes[2] == 16)
        PASS();
    else FAIL("");
}
static void t11(void) {
    linux_seccomp_reset_for_tests();
    TEST("seccomp(unknown op) -> -EINVAL");
    if (linux_seccomp(99, 0, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("bpf cmd<0 -> -EINVAL");
    if (linux_bpf(-1, NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("bpf cmd >= MAX -> -EINVAL");
    if (linux_bpf(LINUX_BPF_CMD_MAX, NULL, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t14(void) {
    TEST("bpf size>0 NULL attr -> -EFAULT");
    if (linux_bpf(LINUX_BPF_PROG_LOAD, NULL, 16) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t15(void) {
    int dummy;
    TEST("bpf well-formed -> -ENOSYS (Marco M1 no BPF)");
    if (linux_bpf(LINUX_BPF_PROG_LOAD, &dummy, 16) == -LINUX_ENOSYS)
        PASS();
    else FAIL("");
}
static void t16(void) {
    TEST("ptrace(TRACEME) -> 0");
    if (linux_ptrace(LINUX_PTRACE_TRACEME, 0, NULL, NULL) == 0) PASS();
    else FAIL("");
}
static void t17(void) {
    TEST("ptrace foreign pid -> -ESRCH");
    if (linux_ptrace(LINUX_PTRACE_ATTACH, 99, NULL, NULL)
        == -LINUX_ESRCH) PASS();
    else FAIL("");
}
static void t18(void) {
    TEST("ptrace(ATTACH, 0) -> -EPERM (cannot self-trace)");
    if (linux_ptrace(LINUX_PTRACE_ATTACH, 0, NULL, NULL)
        == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t19(void) {
    TEST("ptrace negative request -> -EINVAL");
    if (linux_ptrace(-1, 0, NULL, NULL) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t20(void) {
    install_fake(0);
    linux_seccomp_install_ops(NULL);
    int dummy;
    int64_t r = linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER,
                              LINUX_SECCOMP_FILTER_FLAG_TSYNC, &dummy);
    TEST("seccomp install_ops(NULL) clears filter callback");
    if (r == 0 && g_filter_calls == 0) PASS();
    else FAIL("");
}
static void t21(void) {
    install_fake(0);
    linux_seccomp_reset_for_tests();
    int dummy;
    int64_t r = linux_seccomp(LINUX_SECCOMP_SET_MODE_FILTER,
                              LINUX_SECCOMP_FILTER_FLAG_TSYNC, &dummy);
    TEST("seccomp reset clears installed callbacks");
    if (r == 0 && g_filter_calls == 0) PASS();
    else FAIL("");
}

int test_linux_seccomp_run(void) {
    printf("[test_linux_seccomp]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9();
    t10(); t11(); t12(); t13(); t14(); t15(); t16(); t17();
    t18(); t19(); t20(); t21();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
