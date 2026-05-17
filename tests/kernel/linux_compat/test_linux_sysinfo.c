#include "kernel/linux_compat/linux_sysinfo.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* --- providers fixture --- */

static int total_ram_calls;
static int free_ram_calls;
static int uptime_calls;
static int nproc_calls;
static uint64_t fake_total_ram(void)   { total_ram_calls++; return 1ULL << 30; }   /* 1 GiB */
static uint64_t fake_free_ram(void)    { free_ram_calls++; return 512ULL << 20; } /* 512 MiB */
static int64_t  fake_uptime(void)      { uptime_calls++; return 12345; }
static uint16_t fake_nproc(void)       { nproc_calls++; return 7; }

static void install_providers(void) {
    static const struct linux_sysinfo_providers p = {
        .total_ram_bytes = fake_total_ram,
        .free_ram_bytes  = fake_free_ram,
        .uptime_seconds  = fake_uptime,
        .nproc           = fake_nproc,
    };
    total_ram_calls = free_ram_calls = uptime_calls = nproc_calls = 0;
    linux_sysinfo_reset_for_tests();
    linux_sysinfo_install(&p);
}

/* --- struct size check --- */

static void t_sysinfo_struct_112_bytes(void) {
    TEST("sizeof(linux_sysinfo) == 112 (Linux x86_64 ABI)");
    if (sizeof(struct linux_sysinfo) == 112) PASS();
    else FAIL("size mismatch");
}

static void t_rusage_struct_144_bytes(void) {
    TEST("sizeof(linux_rusage) == 144 (Linux x86_64 ABI)");
    if (sizeof(struct linux_rusage) == 144) PASS();
    else FAIL("size mismatch");
}

/* --- sysinfo --- */

static void t_sysinfo_null_efault(void) {
    TEST("sysinfo(NULL) -> -EFAULT");
    if (linux_sysinfo(NULL) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_sysinfo_no_providers_zeroed(void) {
    linux_sysinfo_reset_for_tests();
    struct linux_sysinfo si;
    memset(&si, 0xAB, sizeof(si)); /* poison */
    int64_t r = linux_sysinfo(&si);
    TEST("sysinfo without providers -> zeroed + mem_unit=1, procs=1");
    if (r == 0 && si.totalram == 0 && si.freeram == 0 &&
        si.uptime == 0 && si.mem_unit == 1 && si.procs == 1) PASS();
    else FAIL("not zeroed");
}

static void t_sysinfo_with_providers_filled(void) {
    install_providers();
    struct linux_sysinfo si;
    int64_t r = linux_sysinfo(&si);
    TEST("sysinfo with providers -> totalram, freeram, uptime, procs");
    if (r == 0 && si.totalram == (1ULL<<30) &&
        si.freeram == (512ULL<<20) && si.uptime == 12345 &&
        si.procs == 7 && si.mem_unit == 1 &&
        total_ram_calls == 1 && free_ram_calls == 1 &&
        uptime_calls == 1 && nproc_calls == 1) PASS();
    else FAIL("providers not consulted");
}

static void t_sysinfo_install_null_clears(void) {
    install_providers();
    linux_sysinfo_install(NULL);
    struct linux_sysinfo si;
    int64_t r = linux_sysinfo(&si);
    TEST("sysinfo_install(NULL) clears providers");
    if (r == 0 && si.totalram == 0 && si.freeram == 0 &&
        si.uptime == 0 && si.procs == 1 &&
        total_ram_calls == 0 && free_ram_calls == 0 &&
        uptime_calls == 0 && nproc_calls == 0) PASS();
    else FAIL("NULL didn't clear");
}

static void t_sysinfo_reset_clears(void) {
    install_providers();
    linux_sysinfo_reset_for_tests();
    struct linux_sysinfo si;
    int64_t r = linux_sysinfo(&si);
    TEST("sysinfo reset clears installed providers");
    if (r == 0 && si.totalram == 0 && si.freeram == 0 &&
        si.uptime == 0 && si.procs == 1 &&
        total_ram_calls == 0 && free_ram_calls == 0 &&
        uptime_calls == 0 && nproc_calls == 0) PASS();
    else FAIL("reset didn't clear");
}

/* --- getrusage --- */

static void t_rusage_null_efault(void) {
    TEST("getrusage(SELF, NULL) -> -EFAULT");
    if (linux_getrusage(LINUX_RUSAGE_SELF, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_rusage_invalid_who_einval(void) {
    struct linux_rusage ru;
    TEST("getrusage(99, &ru) -> -EINVAL");
    if (linux_getrusage(99, &ru) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_rusage_self_zeroed(void) {
    struct linux_rusage ru;
    memset(&ru, 0xCC, sizeof(ru));
    int64_t r = linux_getrusage(LINUX_RUSAGE_SELF, &ru);
    TEST("getrusage(SELF) -> 0 + struct fully zeroed");
    int all_zero = 1;
    const unsigned char *b = (const unsigned char *)&ru;
    for (size_t i = 0; i < sizeof(ru); i++) if (b[i]) { all_zero = 0; break; }
    if (r == 0 && all_zero) PASS();
    else FAIL("not zeroed");
}

static void t_rusage_children_accepted(void) {
    struct linux_rusage ru;
    TEST("getrusage(CHILDREN) -> 0 (Marco M1: zero usage)");
    if (linux_getrusage(LINUX_RUSAGE_CHILDREN, &ru) == 0) PASS();
    else FAIL("CHILDREN rejected");
}

static void t_rusage_thread_accepted(void) {
    struct linux_rusage ru;
    TEST("getrusage(THREAD) -> 0 (single-task)");
    if (linux_getrusage(LINUX_RUSAGE_THREAD, &ru) == 0) PASS();
    else FAIL("THREAD rejected");
}

int test_linux_sysinfo_run(void) {
    printf("[test_linux_sysinfo]\n");
    tests_run = tests_passed = 0;

    t_sysinfo_struct_112_bytes();
    t_rusage_struct_144_bytes();

    t_sysinfo_null_efault();
    t_sysinfo_no_providers_zeroed();
    t_sysinfo_with_providers_filled();
    t_sysinfo_install_null_clears();
    t_sysinfo_reset_clears();

    t_rusage_null_efault();
    t_rusage_invalid_who_einval();
    t_rusage_self_zeroed();
    t_rusage_children_accepted();
    t_rusage_thread_accepted();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
