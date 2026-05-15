#include "kernel/linux_compat/linux_proc_vm.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_read_calls;
static int g_write_calls;
static int g_current_calls;
static int g_current = 7;

static int64_t fake_read(const struct linux_proc_vm_iovec *l, size_t lc,
                         const struct linux_proc_vm_iovec *r, size_t rc) {
    (void)l; (void)r;
    g_read_calls++;
    /* Sum lengths of remote_iov to mirror Linux semantics. */
    int64_t total = 0;
    for (size_t i = 0; i < lc && i < rc; i++) total += 1;
    return total;
}
static int64_t fake_write(const struct linux_proc_vm_iovec *l, size_t lc,
                          const struct linux_proc_vm_iovec *r, size_t rc) {
    (void)l; (void)r;
    g_write_calls++;
    int64_t total = 0;
    for (size_t i = 0; i < lc && i < rc; i++) total += 1;
    return total;
}
static int fake_current(void) { g_current_calls++; return g_current; }

static void install_fake(void) {
    static struct linux_proc_vm_ops o;
    o.read_self = fake_read;
    o.write_self = fake_write;
    o.current_pid = fake_current;
    g_read_calls = g_write_calls = g_current_calls = 0;
    linux_proc_vm_reset_for_tests();
    linux_proc_vm_install_ops(&o);
}

static void t1(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv pid<0 -> -EINVAL");
    if (linux_process_vm_readv(-1, NULL, 0, NULL, 0, 0) == -LINUX_EINVAL)
        PASS();
    else FAIL("");
}
static void t2(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv flags!=0 -> -EINVAL");
    if (linux_process_vm_readv(0, NULL, 0, NULL, 0, 1)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv liovcnt > IOV_MAX -> -EINVAL");
    if (linux_process_vm_readv(0, NULL,
                               LINUX_PROC_VM_IOV_MAX + 1, NULL, 0, 0)
        == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv liovcnt>0 NULL local -> -EFAULT");
    if (linux_process_vm_readv(0, NULL, 1, NULL, 0, 0) == -LINUX_EFAULT)
        PASS();
    else FAIL("");
}
static void t5(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv pid 0 self -> 0 (no provider)");
    if (linux_process_vm_readv(0, NULL, 0, NULL, 0, 0) == 0) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_readv foreign pid -> -ESRCH (no current_pid)");
    if (linux_process_vm_readv(99, NULL, 0, NULL, 0, 0) == -LINUX_ESRCH)
        PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    g_current = 7;
    int64_t r = linux_process_vm_readv(7, NULL, 0, NULL, 0, 0);
    TEST("process_vm_readv pid==current_pid -> self path delegates");
    if (r == 0 && g_read_calls == 1) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    g_current = 7;
    TEST("process_vm_readv pid != current -> -ESRCH");
    if (linux_process_vm_readv(99, NULL, 0, NULL, 0, 0) == -LINUX_ESRCH)
        PASS();
    else FAIL("");
}
static void t9(void) {
    linux_proc_vm_reset_for_tests();
    TEST("process_vm_writev foreign pid -> -EPERM (default)");
    if (linux_process_vm_writev(99, NULL, 0, NULL, 0, 0) == -LINUX_EPERM)
        PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    g_current = 7;
    int64_t r = linux_process_vm_writev(7, NULL, 0, NULL, 0, 0);
    TEST("process_vm_writev self delegates to write_self");
    if (r == 0 && g_write_calls == 1) PASS();
    else FAIL("");
}
static void t11(void) {
    TEST("kcmp pid<0 -> -EINVAL");
    if (linux_kcmp(-1, 0, LINUX_KCMP_FILE, 0, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t12(void) {
    TEST("kcmp unknown type -> -EINVAL");
    if (linux_kcmp(0, 0, 99, 0, 0) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t13(void) {
    TEST("kcmp(KCMP_FILE) same fd -> 0 (equal)");
    if (linux_kcmp(0, 0, LINUX_KCMP_FILE, 5, 5) == 0) PASS();
    else FAIL("");
}
static void t14(void) {
    TEST("kcmp(KCMP_FILE) different fd -> 1");
    if (linux_kcmp(0, 0, LINUX_KCMP_FILE, 5, 7) == 1) PASS();
    else FAIL("");
}
static void t15(void) {
    TEST("kcmp(KCMP_VM) same pid -> 0");
    if (linux_kcmp(7, 7, LINUX_KCMP_VM, 0, 0) == 0) PASS();
    else FAIL("");
}
static void t16(void) {
    TEST("kcmp(KCMP_VM) different pids -> 1");
    if (linux_kcmp(7, 8, LINUX_KCMP_VM, 0, 0) == 1) PASS();
    else FAIL("");
}
static void t17(void) {
    install_fake();
    g_current = 7;
    linux_proc_vm_install_ops(NULL);
    int64_t r = linux_process_vm_readv(7, NULL, 0, NULL, 0, 0);
    TEST("proc_vm install_ops(NULL) clears read/current callbacks");
    if (r == -LINUX_ESRCH && g_read_calls == 0 && g_current_calls == 0) PASS();
    else FAIL("");
}
static void t18(void) {
    install_fake();
    g_current = 7;
    linux_proc_vm_install_ops(NULL);
    int64_t r = linux_process_vm_writev(7, NULL, 0, NULL, 0, 0);
    TEST("proc_vm install_ops(NULL) clears write/current callbacks");
    if (r == -LINUX_EPERM && g_write_calls == 0 && g_current_calls == 0) PASS();
    else FAIL("");
}
static void t19(void) {
    install_fake();
    g_current = 7;
    linux_proc_vm_reset_for_tests();
    int64_t r1 = linux_process_vm_readv(7, NULL, 0, NULL, 0, 0);
    int64_t r2 = linux_process_vm_writev(7, NULL, 0, NULL, 0, 0);
    TEST("proc_vm reset clears installed callbacks");
    if (r1 == -LINUX_ESRCH && r2 == -LINUX_EPERM &&
        g_read_calls == 0 && g_write_calls == 0 && g_current_calls == 0) PASS();
    else FAIL("");
}

int test_linux_proc_vm_run(void) {
    printf("[test_linux_proc_vm]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8();
    t9(); t10(); t11(); t12(); t13(); t14(); t15(); t16();
    t17(); t18(); t19();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
