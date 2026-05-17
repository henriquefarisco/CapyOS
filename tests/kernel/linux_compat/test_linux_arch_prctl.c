#include "kernel/linux_compat/linux_arch_prctl.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* Module-local fake MSR values. */
static uint64_t g_fs;
static uint64_t g_gs;
static int      g_set_fs_calls;
static int      g_set_gs_calls;
static int      g_get_fs_calls;
static int      g_get_gs_calls;

static void fake_set_fs(uint64_t addr) { g_fs = addr; g_set_fs_calls++; }
static uint64_t fake_get_fs(void)      { g_get_fs_calls++; return g_fs; }
static void fake_set_gs(uint64_t addr) { g_gs = addr; g_set_gs_calls++; }
static uint64_t fake_get_gs(void)      { g_get_gs_calls++; return g_gs; }

static void install_full(void) {
    linux_arch_prctl_reset_for_tests();
    g_fs = 0;
    g_gs = 0;
    g_set_fs_calls = 0;
    g_set_gs_calls = 0;
    g_get_fs_calls = 0;
    g_get_gs_calls = 0;
    static const struct linux_arch_prctl_ops ops = {
        .set_fs_base = fake_set_fs,
        .get_fs_base = fake_get_fs,
        .set_gs_base = fake_set_gs,
        .get_gs_base = fake_get_gs,
    };
    linux_arch_prctl_install_ops(&ops);
}

/* ---- Tests ---- */

static void t_set_fs_writes_msr(void) {
    install_full();
    int64_t r = linux_arch_prctl(LINUX_ARCH_SET_FS, 0xDEADBEEF12345000ull);
    TEST("ARCH_SET_FS: returns 0 and calls set_fs_base once");
    if (r == 0 && g_set_fs_calls == 1 && g_fs == 0xDEADBEEF12345000ull) PASS();
    else FAIL("set_fs path wrong");
}

static void t_get_fs_reads_msr(void) {
    install_full();
    g_fs = 0xCAFEBABE00000000ull;
    uint64_t out = 0;
    int64_t r = linux_arch_prctl(LINUX_ARCH_GET_FS,
                                 (uint64_t)(uintptr_t)&out);
    TEST("ARCH_GET_FS: returns 0 and writes msr value to *arg");
    if (r == 0 && out == 0xCAFEBABE00000000ull) PASS();
    else FAIL("get_fs path wrong");
}

static void t_set_gs_writes_msr(void) {
    install_full();
    int64_t r = linux_arch_prctl(LINUX_ARCH_SET_GS, 0x1111222233330000ull);
    TEST("ARCH_SET_GS: returns 0 and calls set_gs_base once");
    if (r == 0 && g_set_gs_calls == 1 && g_gs == 0x1111222233330000ull) PASS();
    else FAIL("set_gs path wrong");
}

static void t_get_gs_reads_msr(void) {
    install_full();
    g_gs = 0xAABBCCDDEEFF0000ull;
    uint64_t out = 0;
    int64_t r = linux_arch_prctl(LINUX_ARCH_GET_GS,
                                 (uint64_t)(uintptr_t)&out);
    TEST("ARCH_GET_GS: returns 0 and writes msr value to *arg");
    if (r == 0 && out == 0xAABBCCDDEEFF0000ull) PASS();
    else FAIL("get_gs path wrong");
}

static void t_unknown_op_returns_einval(void) {
    install_full();
    int64_t r = linux_arch_prctl(0x9999, 0);
    TEST("arch_prctl: unknown op -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_get_with_null_ptr_efault(void) {
    install_full();
    int64_t r = linux_arch_prctl(LINUX_ARCH_GET_FS, 0);
    TEST("ARCH_GET_FS with NULL ptr -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_no_ops_returns_enosys_for_set_fs(void) {
    linux_arch_prctl_reset_for_tests();
    int64_t r = linux_arch_prctl(LINUX_ARCH_SET_FS, 0x1234);
    TEST("ARCH_SET_FS without ops -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced for set_fs");
}

static void t_no_ops_returns_enosys_for_get_gs(void) {
    linux_arch_prctl_reset_for_tests();
    uint64_t out = 0;
    int64_t r = linux_arch_prctl(LINUX_ARCH_GET_GS,
                                 (uint64_t)(uintptr_t)&out);
    TEST("ARCH_GET_GS without ops -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced for get_gs");
}

static void t_set_fs_zero_value_accepted(void) {
    /* Linux accepts FS_BASE=0 -- it just disables TLS for that
     * thread. We must not reject it. */
    install_full();
    int64_t r = linux_arch_prctl(LINUX_ARCH_SET_FS, 0);
    TEST("ARCH_SET_FS: addr=0 is valid (Linux semantics)");
    if (r == 0 && g_set_fs_calls == 1 && g_fs == 0) PASS();
    else FAIL("rejected zero addr");
}

static void t_set_then_get_round_trip_fs(void) {
    install_full();
    (void)linux_arch_prctl(LINUX_ARCH_SET_FS, 0xFEEDFACE12340000ull);
    uint64_t out = 0;
    (void)linux_arch_prctl(LINUX_ARCH_GET_FS, (uint64_t)(uintptr_t)&out);
    TEST("arch_prctl: SET_FS then GET_FS round-trips the value");
    if (out == 0xFEEDFACE12340000ull) PASS();
    else FAIL("round trip failed");
}

static void t_install_null_clears_set_callbacks(void) {
    install_full();
    linux_arch_prctl_install_ops(NULL);
    int64_t r1 = linux_arch_prctl(LINUX_ARCH_SET_FS, 0x1234);
    int64_t r2 = linux_arch_prctl(LINUX_ARCH_SET_GS, 0x5678);
    TEST("arch_prctl install_ops(NULL) clears SET callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_set_fs_calls == 0 && g_set_gs_calls == 0) PASS();
    else FAIL("SET callbacks not cleared");
}

static void t_install_null_clears_get_callbacks(void) {
    install_full();
    linux_arch_prctl_install_ops(NULL);
    uint64_t fs_out = 0x1111;
    uint64_t gs_out = 0x2222;
    int64_t r1 = linux_arch_prctl(LINUX_ARCH_GET_FS,
                                  (uint64_t)(uintptr_t)&fs_out);
    int64_t r2 = linux_arch_prctl(LINUX_ARCH_GET_GS,
                                  (uint64_t)(uintptr_t)&gs_out);
    TEST("arch_prctl install_ops(NULL) clears GET callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_get_fs_calls == 0 && g_get_gs_calls == 0 &&
        fs_out == 0x1111 && gs_out == 0x2222) PASS();
    else FAIL("GET callbacks not cleared");
}

static void t_reset_clears_tls_callbacks(void) {
    install_full();
    linux_arch_prctl_reset_for_tests();
    uint64_t out = 0x3333;
    int64_t r1 = linux_arch_prctl(LINUX_ARCH_SET_FS, 0x1234);
    int64_t r2 = linux_arch_prctl(LINUX_ARCH_GET_GS,
                                  (uint64_t)(uintptr_t)&out);
    TEST("arch_prctl reset clears TLS callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        g_set_fs_calls == 0 && g_get_gs_calls == 0 && out == 0x3333) PASS();
    else FAIL("reset callbacks not cleared");
}

int test_linux_arch_prctl_run(void) {
    printf("[test_linux_arch_prctl]\n");
    tests_run = tests_passed = 0;

    t_set_fs_writes_msr();
    t_get_fs_reads_msr();
    t_set_gs_writes_msr();
    t_get_gs_reads_msr();
    t_unknown_op_returns_einval();
    t_get_with_null_ptr_efault();
    t_no_ops_returns_enosys_for_set_fs();
    t_no_ops_returns_enosys_for_get_gs();
    t_set_fs_zero_value_accepted();
    t_set_then_get_round_trip_fs();
    t_install_null_clears_set_callbacks();
    t_install_null_clears_get_callbacks();
    t_reset_clears_tls_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
