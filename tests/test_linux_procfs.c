/* Host tests for linux_procfs.
 *
 * Exercise the read-only /proc backend in isolation: render
 * dispatch via path, fd allocation/release, read with offset,
 * lseek (SET/CUR/END), write rejection (-EROFS), error paths
 * for unsupported paths. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_procfs.h"
#include "kernel/linux_compat/linux_proc.h"
#include "kernel/linux_compat/linux_cpuinfo.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* ---- Fake providers ---- */

static int g_meminfo_calls;
static int fake_meminfo(struct linux_proc_meminfo *out) {
    g_meminfo_calls++;
    out->mem_total_bytes     = 8ull * 1024 * 1024 * 1024;  /* 8 GiB */
    out->mem_free_bytes      = 4ull * 1024 * 1024 * 1024;  /* 4 GiB */
    out->mem_available_bytes = 5ull * 1024 * 1024 * 1024;  /* 5 GiB */
    return 0;
}

static int g_cpuinfo_calls;
static size_t fake_cpuinfo(struct linux_cpuinfo_entry *out, size_t cap) {
    g_cpuinfo_calls++;
    if (cap == 0) return 0;
    out[0] = (struct linux_cpuinfo_entry){
        .processor_index = 0,
        .vendor_id       = "TestCPU",
        .cpu_family      = 6,
        .model           = 42,
        .model_name      = "FakeCPU",
        .stepping        = 1,
        .cpu_mhz         = 2400,
        .cache_size_kb   = 4096,
        .flags           = LINUX_CPUINFO_FLAG_FPU |
                           LINUX_CPUINFO_FLAG_SSE |
                           LINUX_CPUINFO_FLAG_AVX,
    };
    return 1;
}

static int g_maps_calls;
static size_t fake_maps(struct linux_proc_maps_entry *out, size_t cap) {
    g_maps_calls++;
    if (cap < 1) return 0;
    out[0] = (struct linux_proc_maps_entry){
        .start = 0x400000, .end = 0x401000,
        .perm_read = 1, .perm_exec = 1,
        .pathname = "[anon]",
    };
    return 1;
}

static const char *fake_argv[] = { "fake-prog", "--flag", "arg", NULL };
static const char *const *fake_cmdline(void) { return fake_argv; }

static const char *fake_self_exe_path = "/usr/bin/fake";
static const char *fake_self_exe(void) { return fake_self_exe_path; }

static int fake_self_status(struct linux_proc_pid_status *out) {
    *out = (struct linux_proc_pid_status){
        .name = "fake",
        .state = LINUX_PROC_STATE_RUNNING,
        .pid = 42,
        .ppid = 1,
        .uid = 0,
        .gid = 0,
        .fd_size = 64,
        .vm_size_bytes = 1ull << 20,
        .vm_rss_bytes  = 1ull << 18,
        .vm_peak_bytes = 2ull << 20,
    };
    return 0;
}

static const char *fake_version_release_str = "9.9.9-test";
static const char *fake_version_release(void) {
    return fake_version_release_str;
}

static int fake_uptime(struct linux_proc_uptime *out) {
    out->uptime_ns = 5670000000ull;  /* 5.67 seconds */
    out->idle_ns   = 1230000000ull;  /* 1.23 seconds */
    return 0;
}

static int fake_loadavg(struct linux_proc_loadavg *out) {
    out->load1_thousandths  = 250;   /* 0.25 */
    out->load5_thousandths  = 500;   /* 0.50 */
    out->load15_thousandths = 750;   /* 0.75 */
    out->running_tasks = 3;
    out->total_tasks   = 10;
    out->last_pid      = 99;
    return 0;
}

static void install_fakes(void) {
    linux_procfs_reset_for_tests();
    g_meminfo_calls = g_cpuinfo_calls = g_maps_calls = 0;
    static const struct linux_procfs_providers ops = {
        .meminfo         = fake_meminfo,
        .cpuinfo         = fake_cpuinfo,
        .maps            = fake_maps,
        .cmdline         = fake_cmdline,
        .self_exe        = fake_self_exe,
        .self_status     = fake_self_status,
        .version_release = fake_version_release,
        .uptime          = fake_uptime,
        .loadavg         = fake_loadavg,
    };
    linux_procfs_install_providers(&ops);
}

/* ---- Path matching ---- */

static void t_open_meminfo(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    TEST("open /proc/meminfo: fd in procfs range, provider called");
    if (fd >= LINUX_PROCFS_FD_BASE &&
        fd <  LINUX_PROCFS_FD_BASE + LINUX_PROCFS_MAX_INSTANCES &&
        g_meminfo_calls == 1) PASS();
    else FAIL("dispatch wrong");
}

static void t_open_cpuinfo(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/cpuinfo", 0);
    TEST("open /proc/cpuinfo: provider called once");
    if (fd >= LINUX_PROCFS_FD_BASE && g_cpuinfo_calls == 1) PASS();
    else FAIL("dispatch wrong");
}

static void t_open_self_maps(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/maps", 0);
    TEST("open /proc/self/maps: provider called once");
    if (fd >= LINUX_PROCFS_FD_BASE && g_maps_calls == 1) PASS();
    else FAIL("dispatch wrong");
}

static void t_open_self_exe(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/exe", 0);
    TEST("open /proc/self/exe: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_open_self_cmdline(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/cmdline", 0);
    TEST("open /proc/self/cmdline: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_open_self_status(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/status", 0);
    TEST("open /proc/self/status: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_open_unsupported_path(void) {
    install_fakes();
    int64_t r1 = linux_procfs_open("/proc/sys/kernel/random", 0);
    int64_t r2 = linux_procfs_open("/proc/1/status", 0);
    int64_t r3 = linux_procfs_open("/proc/", 0);
    TEST("open unsupported procfs paths -> -ENOENT");
    if (r1 == -LINUX_ENOENT && r2 == -LINUX_ENOENT && r3 == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_open_null_path(void) {
    install_fakes();
    int64_t r = linux_procfs_open(NULL, 0);
    TEST("open NULL path -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_open_unknown_flag(void) {
    install_fakes();
    int64_t r = linux_procfs_open("/proc/meminfo", 0x10000000u);
    TEST("open with unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_open_bad_accmode(void) {
    install_fakes();
    int64_t r = linux_procfs_open("/proc/meminfo", 0x3u);
    TEST("open with O_ACCMODE all-bits -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_open_table_full(void) {
    install_fakes();
    for (int i = 0; i < LINUX_PROCFS_MAX_INSTANCES; i++) {
        int64_t fd = linux_procfs_open("/proc/meminfo", 0);
        if (fd < 0) { TEST("pre-fill"); FAIL("alloc failed early"); return; }
    }
    int64_t r = linux_procfs_open("/proc/meminfo", 0);
    TEST("open when table is full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

/* ---- Read paths ---- */

static void t_read_meminfo_emits_total(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    char buf[4096] = {0};
    int64_t n = linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    /* meminfo formatter emits "MemTotal:" header. */
    int found = (n > 0 && strstr(buf, "MemTotal:") != NULL);
    TEST("read /proc/meminfo: contains 'MemTotal:' header");
    if (found) PASS(); else FAIL("MemTotal: missing");
}

static void t_read_cpuinfo_emits_flags(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/cpuinfo", 0);
    char buf[4096] = {0};
    int64_t n = linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int has_avx = (n > 0 && strstr(buf, "avx") != NULL);
    int has_fpu = (n > 0 && strstr(buf, "fpu") != NULL);
    TEST("read /proc/cpuinfo: emits 'fpu' and 'avx' flag tokens");
    if (has_avx && has_fpu) PASS();
    else FAIL("flags missing");
}

static void t_read_advances_position(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    char b1[10], b2[10];
    int64_t r1 = linux_procfs_read_fd((int)fd, b1, 10);
    int64_t r2 = linux_procfs_read_fd((int)fd, b2, 10);
    /* Two reads should return distinct slices (b1 starts with
     * 'MemTotal:' chars; b2 starts later). */
    int distinct = (r1 == 10 && r2 == 10 && memcmp(b1, b2, 10) != 0);
    TEST("read advances cursor: two 10-byte reads return distinct slices");
    if (distinct) PASS();
    else FAIL("cursor not advanced");
}

static void t_read_eof(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/exe", 0);
    char b1[256];
    /* Drain the entire content. */
    int64_t r1 = linux_procfs_read_fd((int)fd, b1, sizeof(b1));
    /* Next read returns 0 (EOF). */
    int64_t r2 = linux_procfs_read_fd((int)fd, b1, sizeof(b1));
    TEST("read at EOF returns 0");
    if (r1 > 0 && r2 == 0) PASS();
    else FAIL("EOF semantics wrong");
}

static void t_read_zero_len(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t r = linux_procfs_read_fd((int)fd, NULL, 0);
    TEST("read len=0 returns 0 (NULL buf accepted)");
    if (r == 0) PASS();
    else FAIL("zero-len read failed");
}

static void t_read_null_buf(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t r = linux_procfs_read_fd((int)fd, NULL, 16);
    TEST("read NULL buf with len > 0 -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_read_bad_fd(void) {
    install_fakes();
    char b[4];
    int64_t r = linux_procfs_read_fd(0xDEAD, b, 4);
    TEST("read bad fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* ---- write/lseek ---- */

static void t_write_erofs(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    char b[4] = {1};
    int64_t r = linux_procfs_write_fd((int)fd, b, 4);
    TEST("write to procfs fd -> -EROFS (read-only fs)");
    if (r == -LINUX_EROFS) PASS();
    else FAIL("EROFS not surfaced");
}

static void t_lseek_set(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t r = linux_procfs_lseek_fd((int)fd, 5, 0);
    char b[4];
    int64_t r2 = linux_procfs_read_fd((int)fd, b, 4);
    /* Position 5: 4 bytes read should advance to 9. */
    int64_t r3 = linux_procfs_lseek_fd((int)fd, 0, 1);  /* SEEK_CUR */
    TEST("lseek SEEK_SET 5 then read 4 -> tell == 9");
    if (r == 5 && r2 == 4 && r3 == 9) PASS();
    else FAIL("lseek/tell wrong");
}

static void t_lseek_end(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/self/exe", 0);
    int64_t end = linux_procfs_lseek_fd((int)fd, 0, 2);  /* SEEK_END */
    /* Reading at end returns 0 (EOF). */
    char b[4];
    int64_t r = linux_procfs_read_fd((int)fd, b, 4);
    TEST("lseek SEEK_END returns size; subsequent read returns 0");
    if (end > 0 && r == 0) PASS();
    else FAIL("SEEK_END wrong");
}

static void t_lseek_negative_einval(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t r = linux_procfs_lseek_fd((int)fd, -10, 0);
    TEST("lseek to negative offset -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_unknown_whence(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t r = linux_procfs_lseek_fd((int)fd, 0, 99);
    TEST("lseek unknown whence -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_past_eof_clamps(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    int64_t end = linux_procfs_lseek_fd((int)fd, 0, 2);
    int64_t r = linux_procfs_lseek_fd((int)fd, end + 100000, 0);
    TEST("lseek past EOF clamps to size (returns size)");
    if (r == end) PASS();
    else FAIL("clamp not applied");
}

/* ---- close ---- */

static void t_close_releases(void) {
    install_fakes();
    int64_t fd1 = linux_procfs_open("/proc/meminfo", 0);
    int64_t cr = linux_procfs_close((int)fd1);
    int64_t fd2 = linux_procfs_open("/proc/meminfo", 0);
    TEST("close releases slot for re-open (fd2 == fd1)");
    if (cr == 0 && fd2 == fd1) PASS();
    else FAIL("slot not reused");
}

static void t_close_bad_fd(void) {
    install_fakes();
    int64_t r = linux_procfs_close(0xDEAD);
    TEST("close fd outside procfs range -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* ---- Provider absence ---- */

/* -------- new procfs paths (version, uptime, loadavg) -------- */

static void t_open_version_routes(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/version", 0);
    TEST("open /proc/version: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_read_version_emits_prefix(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/version", 0);
    char buf[256] = {0};
    int64_t n = linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int has_prefix = (n > 0 && strncmp(buf, "Linux version ", 14) == 0);
    int has_release = (n > 0 && strstr(buf, "9.9.9-test") != NULL);
    TEST("read /proc/version: prefix 'Linux version ' + fake release");
    if (has_prefix && has_release) PASS();
    else FAIL("format wrong");
}

static void t_read_version_default_release(void) {
    /* No version_release provider -> formatter uses default. */
    linux_procfs_reset_for_tests();
    int64_t fd = linux_procfs_open("/proc/version", 0);
    char buf[256] = {0};
    linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int has_default = (strstr(buf, "6.5.0-capyos") != NULL);
    TEST("read /proc/version: NULL provider -> default '6.5.0-capyos'");
    if (has_default) PASS();
    else FAIL("default missing");
}

static void t_open_uptime_routes(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/uptime", 0);
    TEST("open /proc/uptime: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_read_uptime_emits_seconds(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/uptime", 0);
    char buf[64] = {0};
    linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int matches = (strcmp(buf, "5.67 1.23\n") == 0);
    TEST("read /proc/uptime: emits '<uptime> <idle>\\n' from fake provider");
    if (matches) PASS();
    else FAIL("format wrong");
}

static void t_open_loadavg_routes(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/loadavg", 0);
    TEST("open /proc/loadavg: returns valid fd");
    if (fd >= LINUX_PROCFS_FD_BASE) PASS();
    else FAIL("fd not allocated");
}

static void t_read_loadavg_emits_format(void) {
    install_fakes();
    int64_t fd = linux_procfs_open("/proc/loadavg", 0);
    char buf[128] = {0};
    linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int matches = (strcmp(buf, "0.25 0.50 0.75 3/10 99\n") == 0);
    TEST("read /proc/loadavg: emits 3 averages + tasks + lastpid");
    if (matches) PASS();
    else FAIL("format wrong");
}

static void t_no_providers_render_empty(void) {
    /* When no providers installed, formatters still run with
     * zeroed inputs and produce parseable but trivial content.
     * meminfo always emits header lines even with zeroes. */
    linux_procfs_reset_for_tests();
    int64_t fd = linux_procfs_open("/proc/meminfo", 0);
    char buf[256] = {0};
    int64_t r = linux_procfs_read_fd((int)fd, buf, sizeof(buf) - 1);
    int has_header = (r > 0 && strstr(buf, "MemTotal:") != NULL);
    TEST("no providers: render still produces header");
    if (has_header) PASS();
    else FAIL("zero-state render failed");
}

int test_linux_procfs_run(void) {
    printf("[test_linux_procfs]\n");
    tests_run = tests_passed = 0;

    t_open_meminfo();
    t_open_cpuinfo();
    t_open_self_maps();
    t_open_self_exe();
    t_open_self_cmdline();
    t_open_self_status();
    t_open_unsupported_path();
    t_open_null_path();
    t_open_unknown_flag();
    t_open_bad_accmode();
    t_open_table_full();

    t_read_meminfo_emits_total();
    t_read_cpuinfo_emits_flags();
    t_read_advances_position();
    t_read_eof();
    t_read_zero_len();
    t_read_null_buf();
    t_read_bad_fd();

    t_write_erofs();
    t_lseek_set();
    t_lseek_end();
    t_lseek_negative_einval();
    t_lseek_unknown_whence();
    t_lseek_past_eof_clamps();

    t_close_releases();
    t_close_bad_fd();

    t_open_version_routes();
    t_read_version_emits_prefix();
    t_read_version_default_release();
    t_open_uptime_routes();
    t_read_uptime_emits_seconds();
    t_open_loadavg_routes();
    t_read_loadavg_emits_format();

    t_no_providers_render_empty();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
