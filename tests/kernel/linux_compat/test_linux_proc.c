/*
 * Host tests for linux_proc (S2.5 /proc/meminfo + S2.6 /proc/<pid>/status).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_proc.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int contains(const char *h, const char *n) {
    return strstr(h, n) != NULL;
}

/* -------- meminfo -------- */

static void t_meminfo_basic(void) {
    struct linux_proc_meminfo m = {
        .mem_total_bytes      = 4ull * 1024 * 1024 * 1024,  /* 4 GiB */
        .mem_free_bytes       = 2ull * 1024 * 1024 * 1024,  /* 2 GiB */
        .mem_available_bytes  = 2ull * 1024 * 1024 * 1024,
    };
    char buf[1024];
    size_t r = linux_proc_format_meminfo(&m, buf, sizeof(buf));
    TEST("meminfo: emits all 7 mandatory fields");
    int ok = r > 0 &&
             contains(buf, "MemTotal: 4194304 kB\n") &&
             contains(buf, "MemFree: 2097152 kB\n") &&
             contains(buf, "MemAvailable: 2097152 kB\n") &&
             contains(buf, "Buffers: 0 kB\n") &&
             contains(buf, "Cached: 0 kB\n") &&
             contains(buf, "SwapTotal: 0 kB\n") &&
             contains(buf, "SwapFree: 0 kB\n");
    if (ok) PASS(); else FAIL("missing field or wrong value");
}

static void t_meminfo_size_query(void) {
    struct linux_proc_meminfo m = { .mem_total_bytes = 1024 };
    size_t needed = linux_proc_format_meminfo(&m, NULL, 0);
    char buf[needed + 16];
    size_t actual = linux_proc_format_meminfo(&m, buf, sizeof(buf));
    TEST("meminfo: NULL buf returns required size; matches actual");
    if (needed == actual && needed > 0) PASS(); else FAIL("size query mismatch");
}

static void t_meminfo_truncation(void) {
    struct linux_proc_meminfo m = { .mem_total_bytes = 1024 };
    char buf[8];
    size_t r = linux_proc_format_meminfo(&m, buf, sizeof(buf));
    TEST("meminfo: truncation reports required bytes, NUL-terminates");
    if (r > 8 && buf[7] == '\0') PASS(); else FAIL("truncation wrong");
}

static void t_meminfo_null_input(void) {
    char buf[1024];
    size_t r = linux_proc_format_meminfo(NULL, buf, sizeof(buf));
    TEST("meminfo: NULL input emits zeros (defensive)");
    if (r > 0 && contains(buf, "MemTotal: 0 kB\n")) PASS();
    else FAIL("NULL not handled");
}

static void t_meminfo_kb_rounding(void) {
    /* 1023 bytes -> 0 kB, 1024 -> 1 kB, 2047 -> 1 kB. */
    struct linux_proc_meminfo m = { .mem_total_bytes = 2047 };
    char buf[1024];
    linux_proc_format_meminfo(&m, buf, sizeof(buf));
    TEST("meminfo: bytes converted to kB (rounded down)");
    if (contains(buf, "MemTotal: 1 kB\n")) PASS(); else FAIL("rounding wrong");
}

/* -------- pid_status -------- */

static void t_pid_status_running(void) {
    struct linux_proc_pid_status s = {
        .name = "capysh", .state = LINUX_PROC_STATE_RUNNING,
        .pid = 42, .ppid = 1, .uid = 1000, .gid = 1000,
        .fd_size = 64,
        .vm_size_bytes = 16ull * 1024 * 1024,   /* 16 MiB */
        .vm_rss_bytes  = 4ull  * 1024 * 1024,   /* 4 MiB */
        .vm_peak_bytes = 20ull * 1024 * 1024,
    };
    char buf[1024];
    size_t r = linux_proc_format_pid_status(&s, buf, sizeof(buf));
    TEST("pid_status: running task with all 11 mandatory fields");
    int ok = r > 0 &&
             contains(buf, "Name:\tcapysh\n") &&
             contains(buf, "State:\tR (running)\n") &&
             contains(buf, "Tgid:\t42\n") &&
             contains(buf, "Pid:\t42\n") &&
             contains(buf, "PPid:\t1\n") &&
             contains(buf, "Uid:\t1000\t1000\t1000\t1000\n") &&
             contains(buf, "Gid:\t1000\t1000\t1000\t1000\n") &&
             contains(buf, "FDSize:\t64\n") &&
             contains(buf, "VmPeak:\t20480 kB\n") &&
             contains(buf, "VmSize:\t16384 kB\n") &&
             contains(buf, "VmRSS:\t4096 kB\n");
    if (ok) PASS(); else FAIL("field shape wrong");
}

static void t_pid_status_state_letters(void) {
    struct linux_proc_pid_status s = { .name = "x" };
    char buf[1024];

    s.state = LINUX_PROC_STATE_SLEEPING;
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    int ok = contains(buf, "State:\tS (sleeping)\n");

    s.state = LINUX_PROC_STATE_DISK_SLEEP;
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    ok = ok && contains(buf, "State:\tD (disk sleep)\n");

    s.state = LINUX_PROC_STATE_ZOMBIE;
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    ok = ok && contains(buf, "State:\tZ (zombie)\n");

    s.state = LINUX_PROC_STATE_STOPPED;
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    ok = ok && contains(buf, "State:\tT (stopped)\n");

    s.state = LINUX_PROC_STATE_DEAD;
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    ok = ok && contains(buf, "State:\tX (dead)\n");

    TEST("pid_status: all state letters R/S/D/Z/T/X covered");
    if (ok) PASS(); else FAIL("state letter wrong");
}

static void t_pid_status_null_name(void) {
    struct linux_proc_pid_status s = { .name = NULL, .pid = 1 };
    char buf[1024];
    linux_proc_format_pid_status(&s, buf, sizeof(buf));
    TEST("pid_status: NULL name -> 'unknown'");
    if (contains(buf, "Name:\tunknown\n")) PASS(); else FAIL("NULL name not handled");
}

static void t_pid_status_size_query(void) {
    struct linux_proc_pid_status s = { .name = "x" };
    size_t needed = linux_proc_format_pid_status(&s, NULL, 0);
    char buf[needed + 16];
    size_t actual = linux_proc_format_pid_status(&s, buf, sizeof(buf));
    TEST("pid_status: size-query (NULL buf) returns exact size");
    if (needed == actual && needed > 0) PASS(); else FAIL("size mismatch");
}

static void t_pid_status_truncation(void) {
    struct linux_proc_pid_status s = { .name = "x" };
    char buf[16];
    size_t r = linux_proc_format_pid_status(&s, buf, sizeof(buf));
    TEST("pid_status: truncation reports required, NUL-terminates");
    if (r > 16 && buf[15] == '\0') PASS(); else FAIL("truncation wrong");
}

static void t_pid_status_null_input(void) {
    char buf[1024];
    size_t r = linux_proc_format_pid_status(NULL, buf, sizeof(buf));
    TEST("pid_status: NULL input -> defaults (Name:unknown, R, zeros)");
    int ok = r > 0 &&
             contains(buf, "Name:\tunknown\n") &&
             contains(buf, "State:\tR (running)\n") &&
             contains(buf, "Pid:\t0\n");
    if (ok) PASS(); else FAIL("NULL input not handled");
}

/* -------- /proc/self/maps (S2.1) -------- */

static void t_maps_basic_anon(void) {
    struct linux_proc_maps_entry e = {
        .start = 0x500000000000ull, .end = 0x500000001000ull,
        .perm_read = 1, .perm_write = 1, .perm_exec = 0,
        .perm_shared = 0,
        .offset = 0, .dev_major = 0, .dev_minor = 0, .inode = 0,
        .pathname = NULL,
    };
    char buf[1024];
    size_t r = linux_proc_format_maps(&e, 1, buf, sizeof(buf));
    TEST("maps: anon region (rw-p) emits canonical line");
    if (r > 0 && contains(buf, "500000000000-500000001000 rw-p 00000000 00:00 0\n"))
        PASS();
    else FAIL("anon line wrong");
}

static void t_maps_with_pathname(void) {
    struct linux_proc_maps_entry e = {
        .start = 0x400000ull, .end = 0x401000ull,
        .perm_read = 1, .perm_exec = 1, .perm_shared = 0,
        .dev_major = 0xfe, .dev_minor = 0x01, .inode = 12345,
        .pathname = "/usr/bin/cat",
    };
    char buf[1024];
    size_t r = linux_proc_format_maps(&e, 1, buf, sizeof(buf));
    TEST("maps: file-backed line includes pathname suffix");
    /* lowercase hex, dev fe:01, inode 12345, pathname after spaces */
    if (r > 0 &&
        contains(buf, "400000-401000 r-xp 00000000 fe:01 12345") &&
        contains(buf, "/usr/bin/cat\n")) PASS();
    else FAIL("file-backed shape wrong");
}

static void t_maps_perms_combinations(void) {
    struct linux_proc_maps_entry e = {
        .start = 0x1000, .end = 0x2000,
        .perm_read = 0, .perm_write = 0, .perm_exec = 0,
        .perm_shared = 1,
    };
    char buf[1024];
    linux_proc_format_maps(&e, 1, buf, sizeof(buf));
    int ok1 = contains(buf, "1000-2000 ---s ");

    e.perm_read = 1; e.perm_write = 1; e.perm_exec = 1;
    e.perm_shared = 0;
    linux_proc_format_maps(&e, 1, buf, sizeof(buf));
    int ok2 = contains(buf, "rwxp ");

    TEST("maps: perms encode r/w/x/p (private) and s (shared)");
    if (ok1 && ok2) PASS(); else FAIL("perms wrong");
}

static void t_maps_zero_entries(void) {
    char buf[64];
    buf[0] = 'X';
    size_t r = linux_proc_format_maps(NULL, 0, buf, sizeof(buf));
    TEST("maps: zero entries -> 0, NUL-terminated");
    if (r == 0 && buf[0] == '\0') PASS(); else FAIL("zero path wrong");
}

static void t_maps_multi_lines(void) {
    struct linux_proc_maps_entry es[3] = {
        { .start = 0x400000, .end = 0x401000, .perm_read = 1 },
        { .start = 0x402000, .end = 0x403000, .perm_read = 1, .perm_write = 1 },
        { .start = 0x404000, .end = 0x405000, .perm_read = 1, .perm_exec = 1 },
    };
    char buf[1024];
    linux_proc_format_maps(es, 3, buf, sizeof(buf));
    TEST("maps: 3 entries emit 3 lines");
    int newlines = 0;
    for (size_t i = 0; buf[i]; i++) if (buf[i] == '\n') newlines++;
    if (newlines == 3) PASS();
    else FAIL("newline count wrong");
}

static void t_maps_size_query(void) {
    struct linux_proc_maps_entry e = { .start = 0x1000, .end = 0x2000 };
    size_t needed = linux_proc_format_maps(&e, 1, NULL, 0);
    char buf[needed + 16];
    size_t actual = linux_proc_format_maps(&e, 1, buf, sizeof(buf));
    TEST("maps: size-query (NULL buf) returns exact size");
    if (needed == actual && needed > 0) PASS();
    else FAIL("size mismatch");
}

/* -------- /proc/self/cmdline (S2.3) -------- */

static void t_cmdline_basic(void) {
    const char *argv[] = { "capysh", "-i", "script.js", NULL };
    char buf[64];
    size_t r = linux_proc_format_cmdline(argv, buf, sizeof(buf));
    /* Each arg + NUL terminator. Total = 7+3+10 = 20. */
    TEST("cmdline: argv joined by NULs (Linux byte stream)");
    if (r == 20 && buf[6] == '\0' && buf[9] == '\0' && buf[19] == '\0') PASS();
    else FAIL("byte stream wrong");
}

static void t_cmdline_empty(void) {
    const char *argv[] = { NULL };
    char buf[16] = { 'X' };
    size_t r = linux_proc_format_cmdline(argv, buf, sizeof(buf));
    TEST("cmdline: empty argv -> 0 bytes");
    if (r == 0) PASS(); else FAIL("empty path wrong");
}

static void t_cmdline_null_argv(void) {
    char buf[16] = { 'X' };
    size_t r = linux_proc_format_cmdline(NULL, buf, sizeof(buf));
    TEST("cmdline: NULL argv -> 0 bytes (defensive)");
    if (r == 0) PASS(); else FAIL("NULL not handled");
}

static void t_cmdline_size_query(void) {
    const char *argv[] = { "a", "bb", "ccc", NULL };
    size_t needed = linux_proc_format_cmdline(argv, NULL, 0);
    /* "a\0" + "bb\0" + "ccc\0" = 2 + 3 + 4 = 9 */
    TEST("cmdline: size query (NULL buf) reports exact size = 9");
    if (needed == 9) PASS(); else FAIL("size wrong");
}

static void t_cmdline_truncation(void) {
    const char *argv[] = { "very-long-argument-name", "more", NULL };
    char buf[8];
    size_t r = linux_proc_format_cmdline(argv, buf, sizeof(buf));
    TEST("cmdline: truncation reports required, NUL-terminates");
    if (r > 8 && buf[7] == '\0') PASS(); else FAIL("truncation wrong");
}

/* -------- /proc/self/exe (S2.2) -------- */

static void t_self_exe_basic(void) {
    char buf[64];
    size_t r = linux_proc_format_self_exe("/usr/bin/capysh", buf, sizeof(buf));
    TEST("self_exe: copies path verbatim, returns length");
    if (r == 15 && contains(buf, "/usr/bin/capysh") && buf[15] == '\0') PASS();
    else FAIL("path copy wrong");
}

static void t_self_exe_null_path(void) {
    char buf[32];
    size_t r = linux_proc_format_self_exe(NULL, buf, sizeof(buf));
    TEST("self_exe: NULL path -> '/unknown' default");
    if (r == 8 && contains(buf, "/unknown")) PASS();
    else FAIL("NULL not handled");
}

static void t_self_exe_size_query(void) {
    size_t needed = linux_proc_format_self_exe("/foo/bar", NULL, 0);
    TEST("self_exe: size-query returns exact length (8)");
    if (needed == 8) PASS();
    else FAIL("size mismatch");
}

static void t_self_exe_truncation(void) {
    char buf[4];
    size_t r = linux_proc_format_self_exe("/very/long/path",
                                          buf, sizeof(buf));
    TEST("self_exe: truncation reports required size, NUL-terminates");
    if (r == 15 && buf[3] == '\0') PASS();
    else FAIL("truncation wrong");
}

/* -------- /proc/version -------- */

static void t_version_basic(void) {
    char buf[256];
    size_t r = linux_proc_format_version("6.5.0-capyos", buf, sizeof(buf));
    int has_prefix = (strstr(buf, "Linux version ") == buf);
    int has_release = (strstr(buf, "6.5.0-capyos") != NULL);
    int has_x86 = (strstr(buf, "x86_64") != NULL);
    int ends_nl = (r > 0 && buf[r - 1] == '\n');
    TEST("version: 'Linux version' prefix, release embedded, x86_64, '\\n' end");
    if (has_prefix && has_release && has_x86 && ends_nl) PASS();
    else FAIL("format wrong");
}

static void t_version_null_release(void) {
    char buf[256];
    linux_proc_format_version(NULL, buf, sizeof(buf));
    int has_default = (strstr(buf, "6.5.0-capyos") != NULL);
    TEST("version: NULL release uses default '6.5.0-capyos'");
    if (has_default) PASS();
    else FAIL("default missing");
}

static void t_version_size_query(void) {
    size_t need = linux_proc_format_version("foo", NULL, 0);
    char buf[256];
    size_t r = linux_proc_format_version("foo", buf, sizeof(buf));
    TEST("version: NULL/0 size query matches actual write");
    if (need == r && need > 0) PASS();
    else FAIL("size mismatch");
}

/* -------- /proc/uptime -------- */

static void t_uptime_basic(void) {
    /* 12 seconds, 34 hundredths => 12.34 12.34 (idle = uptime) */
    struct linux_proc_uptime u = {
        .uptime_ns = 12340000000ull,
        .idle_ns   = 12340000000ull,
    };
    char buf[64] = {0};
    linux_proc_format_uptime(&u, buf, sizeof(buf));
    int matches = (strcmp(buf, "12.34 12.34\n") == 0);
    TEST("uptime: 12.34s formatted with 2-digit hundredths");
    if (matches) PASS();
    else FAIL("format wrong");
}

static void t_uptime_zero(void) {
    struct linux_proc_uptime u = {0};
    char buf[64] = {0};
    linux_proc_format_uptime(&u, buf, sizeof(buf));
    TEST("uptime: zero values format as '0.00 0.00\\n'");
    if (strcmp(buf, "0.00 0.00\n") == 0) PASS();
    else FAIL("format wrong");
}

static void t_uptime_subsecond_padding(void) {
    /* 1.05 seconds: hundredths=5, must be zero-padded to "05". */
    struct linux_proc_uptime u = {
        .uptime_ns = 1050000000ull,
        .idle_ns   = 0,
    };
    char buf[64] = {0};
    linux_proc_format_uptime(&u, buf, sizeof(buf));
    int has_05 = (strncmp(buf, "1.05 ", 5) == 0);
    TEST("uptime: hundredths < 10 are zero-padded ('1.05', not '1.5')");
    if (has_05) PASS();
    else FAIL("padding missing");
}

static void t_uptime_null_input(void) {
    char buf[64] = {0};
    linux_proc_format_uptime(NULL, buf, sizeof(buf));
    TEST("uptime: NULL input emits '0.00 0.00\\n' (defensive zero)");
    if (strcmp(buf, "0.00 0.00\n") == 0) PASS();
    else FAIL("not defensive");
}

/* -------- /proc/loadavg -------- */

static void t_loadavg_basic(void) {
    struct linux_proc_loadavg l = {
        .load1_thousandths  = 1230,  /* 1.23 */
        .load5_thousandths  = 450,   /* 0.45 */
        .load15_thousandths = 70,    /* 0.07 */
        .running_tasks = 2,
        .total_tasks   = 5,
        .last_pid      = 42,
    };
    char buf[128] = {0};
    linux_proc_format_loadavg(&l, buf, sizeof(buf));
    int ok = (strcmp(buf, "1.23 0.45 0.07 2/5 42\n") == 0);
    TEST("loadavg: thousandths -> '1.23 0.45 0.07' + tasks + lastpid");
    if (ok) PASS();
    else FAIL("format wrong");
}

static void t_loadavg_zero(void) {
    struct linux_proc_loadavg l = {0};
    char buf[64] = {0};
    linux_proc_format_loadavg(&l, buf, sizeof(buf));
    TEST("loadavg: all zeros -> '0.00 0.00 0.00 0/0 0\\n'");
    if (strcmp(buf, "0.00 0.00 0.00 0/0 0\n") == 0) PASS();
    else FAIL("format wrong");
}

static void t_loadavg_thousandths_truncation(void) {
    /* 1234 thousandths = 1.234, truncated to 1.23 (floor). */
    struct linux_proc_loadavg l = {
        .load1_thousandths = 1234,
    };
    char buf[64] = {0};
    linux_proc_format_loadavg(&l, buf, sizeof(buf));
    int ok = (strncmp(buf, "1.23 ", 5) == 0);
    TEST("loadavg: 1234 thousandths -> '1.23' (truncating floor)");
    if (ok) PASS();
    else FAIL("rounding wrong");
}

static void t_loadavg_null_input(void) {
    char buf[64] = {0};
    linux_proc_format_loadavg(NULL, buf, sizeof(buf));
    TEST("loadavg: NULL input emits '0.00 0.00 0.00 0/0 0\\n'");
    if (strcmp(buf, "0.00 0.00 0.00 0/0 0\n") == 0) PASS();
    else FAIL("not defensive");
}

int test_linux_proc_run(void) {
    printf("[test_linux_proc]\n");
    tests_run = tests_passed = 0;

    t_meminfo_basic();
    t_meminfo_size_query();
    t_meminfo_truncation();
    t_meminfo_null_input();
    t_meminfo_kb_rounding();

    t_pid_status_running();
    t_pid_status_state_letters();
    t_pid_status_null_name();
    t_pid_status_size_query();
    t_pid_status_truncation();
    t_pid_status_null_input();

    t_maps_basic_anon();
    t_maps_with_pathname();
    t_maps_perms_combinations();
    t_maps_zero_entries();
    t_maps_multi_lines();
    t_maps_size_query();

    t_cmdline_basic();
    t_cmdline_empty();
    t_cmdline_null_argv();
    t_cmdline_size_query();
    t_cmdline_truncation();

    t_self_exe_basic();
    t_self_exe_null_path();
    t_self_exe_size_query();
    t_self_exe_truncation();

    t_version_basic();
    t_version_null_release();
    t_version_size_query();

    t_uptime_basic();
    t_uptime_zero();
    t_uptime_subsecond_padding();
    t_uptime_null_input();

    t_loadavg_basic();
    t_loadavg_zero();
    t_loadavg_thousandths_truncation();
    t_loadavg_null_input();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
