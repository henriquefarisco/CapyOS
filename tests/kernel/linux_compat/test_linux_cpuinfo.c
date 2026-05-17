/*
 * Host tests for linux_cpuinfo (S2.4).
 *
 * /proc/cpuinfo format is a strict contract: Firefox/SpiderMonkey
 * parsers expect literal field names (especially "flags\t\t: "),
 * trailing blank line between blocks, and space-separated tokens
 * in the flags list. These tests lock each of these invariants.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_cpuinfo.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/* ------------------------------------------------------------------ */

static void t_single_cpu_basic(void) {
    struct linux_cpuinfo_entry e = {
        .processor_index = 0,
        .vendor_id = "GenuineIntel",
        .cpu_family = 6,
        .model = 158,
        .model_name = "Intel(R) Core(TM) i7-8700K",
        .stepping = 10,
        .cpu_mhz = 3700,
        .cache_size_kb = 12288,
        .flags = LINUX_CPUINFO_FLAG_FPU | LINUX_CPUINFO_FLAG_SSE2 |
                 LINUX_CPUINFO_FLAG_LM,
    };
    char buf[2048];
    size_t r = linux_cpuinfo_format(&e, 1, buf, sizeof(buf));

    TEST("single CPU: output contains all canonical field labels");
    int ok = contains(buf, "processor\t: 0\n") &&
             contains(buf, "vendor_id\t: GenuineIntel\n") &&
             contains(buf, "cpu family\t: 6\n") &&
             contains(buf, "model\t\t: 158\n") &&
             contains(buf, "model name\t: Intel(R) Core(TM) i7-8700K\n") &&
             contains(buf, "stepping\t: 10\n") &&
             contains(buf, "cpu MHz\t\t: 3700\n") &&
             contains(buf, "cache size\t: 12288 KB\n") &&
             contains(buf, "physical id\t: 0\n") &&
             contains(buf, "siblings\t: 1\n") &&
             contains(buf, "core id\t\t: 0\n") &&
             contains(buf, "cpu cores\t: 1\n") &&
             contains(buf, "flags\t\t: fpu sse2 lm\n") &&
             contains(buf, "bogomips\t: 7400\n");
    if (ok && r > 0) PASS(); else FAIL("missing fields or zero return");
}

static void t_trailing_blank_line(void) {
    struct linux_cpuinfo_entry e = { .processor_index = 0, .cpu_mhz = 100 };
    char buf[1024];
    size_t r = linux_cpuinfo_format(&e, 1, buf, sizeof(buf));
    TEST("block ends with blank line (Linux parser contract)");
    /* Last two chars before NUL must be "\n\n". */
    if (r >= 2 && buf[r - 1] == '\n' && buf[r - 2] == '\n') PASS();
    else FAIL("missing trailing blank line");
}

static void t_multi_cpu_blocks(void) {
    struct linux_cpuinfo_entry es[2] = {
        { .processor_index = 0, .cpu_mhz = 3000, .flags = LINUX_CPUINFO_FLAG_SSE2 },
        { .processor_index = 1, .cpu_mhz = 3000, .flags = LINUX_CPUINFO_FLAG_SSE2 },
    };
    char buf[4096];
    size_t r = linux_cpuinfo_format(es, 2, buf, sizeof(buf));

    TEST("multi-CPU: two blocks, siblings=2, cpu cores=2");
    int ok = r > 0 &&
             contains(buf, "processor\t: 0\n") &&
             contains(buf, "processor\t: 1\n") &&
             contains(buf, "siblings\t: 2\n") &&
             contains(buf, "cpu cores\t: 2\n");
    if (ok) PASS(); else FAIL("multi-block shape wrong");
}

static void t_all_known_flags(void) {
    uint32_t mask = LINUX_CPUINFO_FLAG_FPU | LINUX_CPUINFO_FLAG_TSC |
                    LINUX_CPUINFO_FLAG_CMOV | LINUX_CPUINFO_FLAG_MMX |
                    LINUX_CPUINFO_FLAG_SSE | LINUX_CPUINFO_FLAG_SSE2 |
                    LINUX_CPUINFO_FLAG_SSE3 | LINUX_CPUINFO_FLAG_SSSE3 |
                    LINUX_CPUINFO_FLAG_SSE4_1 | LINUX_CPUINFO_FLAG_SSE4_2 |
                    LINUX_CPUINFO_FLAG_AVX | LINUX_CPUINFO_FLAG_AVX2 |
                    LINUX_CPUINFO_FLAG_FMA | LINUX_CPUINFO_FLAG_POPCNT |
                    LINUX_CPUINFO_FLAG_AES | LINUX_CPUINFO_FLAG_RDRAND |
                    LINUX_CPUINFO_FLAG_RDSEED | LINUX_CPUINFO_FLAG_LM;
    struct linux_cpuinfo_entry e = { .flags = mask };
    char buf[2048];
    linux_cpuinfo_format(&e, 1, buf, sizeof(buf));

    TEST("flags: emits canonical Linux tokens in priority order");
    /* Linux uses "pni" for SSE3. All tokens space-separated. */
    int ok = contains(buf, "flags\t\t: fpu tsc cmov mmx sse sse2 pni ssse3 sse4_1 sse4_2 avx avx2 fma popcnt aes rdrand rdseed lm\n");
    if (ok) PASS(); else FAIL("flag token order wrong");
}

static void t_no_flags_emits_empty(void) {
    struct linux_cpuinfo_entry e = { .flags = 0 };
    char buf[1024];
    linux_cpuinfo_format(&e, 1, buf, sizeof(buf));
    TEST("flags: zero mask emits empty list, not missing line");
    if (contains(buf, "flags\t\t: \n")) PASS();
    else FAIL("flags line wrong");
}

static void t_null_defaults(void) {
    struct linux_cpuinfo_entry e = { 0 };
    char buf[1024];
    linux_cpuinfo_format(&e, 1, buf, sizeof(buf));
    TEST("NULL vendor/model: substitutes CapyOS defaults");
    int ok = contains(buf, "vendor_id\t: CapyOS\n") &&
             contains(buf, "model name\t: CapyOS generic\n");
    if (ok) PASS(); else FAIL("defaults missing");
}

static void t_size_query_mode(void) {
    struct linux_cpuinfo_entry e = {
        .processor_index = 0, .cpu_mhz = 100,
        .flags = LINUX_CPUINFO_FLAG_SSE2,
    };
    size_t needed = linux_cpuinfo_format(&e, 1, NULL, 0);

    char buf[needed + 16];
    size_t actual = linux_cpuinfo_format(&e, 1, buf, sizeof(buf));

    TEST("size-query mode (NULL buf): returns exact required bytes");
    if (needed == actual && needed > 0) PASS();
    else FAIL("size query did not match actual");
}

static void t_truncation_reports_required(void) {
    struct linux_cpuinfo_entry e = { .processor_index = 0, .cpu_mhz = 100 };
    char buf[16];
    size_t r = linux_cpuinfo_format(&e, 1, buf, sizeof(buf));

    TEST("truncation: returns required size, buf NUL-terminated");
    if (r > 16 && buf[15] == '\0') PASS();
    else FAIL("truncation contract violated");
}

static void t_zero_entries(void) {
    char buf[64];
    buf[0] = 'X';
    size_t r = linux_cpuinfo_format(NULL, 0, buf, sizeof(buf));
    TEST("zero entries: returns 0, NUL-terminates buf");
    if (r == 0 && buf[0] == '\0') PASS();
    else FAIL("zero-entries path wrong");
}

static void t_null_entries_nonzero_n(void) {
    char buf[64];
    buf[0] = 'X';
    size_t r = linux_cpuinfo_format(NULL, 2, buf, sizeof(buf));
    TEST("NULL entries with n > 0: returns 0 (defensive)");
    if (r == 0) PASS();
    else FAIL("defensive check failed");
}

static void t_bogomips_formula(void) {
    struct linux_cpuinfo_entry e = { .cpu_mhz = 2500 };
    char buf[1024];
    linux_cpuinfo_format(&e, 1, buf, sizeof(buf));
    TEST("bogomips = cpu_mhz * 2 (Linux approximation)");
    if (contains(buf, "bogomips\t: 5000\n")) PASS();
    else FAIL("bogomips formula wrong");
}

int test_linux_cpuinfo_run(void) {
    printf("[test_linux_cpuinfo]\n");
    tests_run = tests_passed = 0;

    t_single_cpu_basic();
    t_trailing_blank_line();
    t_multi_cpu_blocks();
    t_all_known_flags();
    t_no_flags_emits_empty();
    t_null_defaults();
    t_size_query_mode();
    t_truncation_reports_required();
    t_zero_entries();
    t_null_entries_nonzero_n();
    t_bogomips_formula();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
