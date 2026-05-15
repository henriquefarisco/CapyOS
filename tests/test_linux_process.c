/*
 * Host tests for linux_process (S1.9 prctl, S1.11 sched_yield/
 * affinity, S1.17 prlimit64, S1.18 gettid).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_process.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake task view. We simulate a tiny task table with 2 tasks so we
 * can exercise pid=0 (current), pid=self, pid=other, pid=unknown. */
struct fake_task {
    uint32_t pid;
    char name[32];
};
static struct fake_task g_fake_tasks[2];
static struct fake_task *g_fake_current;
static int g_yield_count;

static linux_task_t *fake_current(void) { return (linux_task_t *)g_fake_current; }

static linux_task_t *fake_by_pid(uint32_t pid) {
    for (int i = 0; i < 2; i++) {
        if (g_fake_tasks[i].pid == pid) return (linux_task_t *)&g_fake_tasks[i];
    }
    return NULL;
}

static int fake_view(linux_task_t *t, struct linux_task_view *out) {
    if (!t) return -1;
    struct fake_task *ft = (struct fake_task *)t;
    out->pid = ft->pid;
    out->name = ft->name;
    out->name_cap = sizeof(ft->name);
    return 0;
}

static void fake_yield(void) { g_yield_count++; }

static void install_fake_ops(void) {
    linux_process_reset_for_tests();
    memset(g_fake_tasks, 0, sizeof(g_fake_tasks));
    g_fake_tasks[0].pid = 42;
    strcpy(g_fake_tasks[0].name, "capysh");
    g_fake_tasks[1].pid = 100;
    strcpy(g_fake_tasks[1].name, "browser");
    g_fake_current = &g_fake_tasks[0];
    g_yield_count = 0;
    static const struct linux_process_ops ops = {
        .current = fake_current,
        .by_pid  = fake_by_pid,
        .view    = fake_view,
        .yield   = fake_yield,
    };
    linux_process_install_ops(&ops);
}

/* ------------ gettid (S1.18) ------------------------------------- */

static void t_gettid_returns_pid(void) {
    install_fake_ops();
    int64_t r = linux_gettid();
    TEST("gettid: returns pid of current task");
    if (r == 42) PASS(); else FAIL("wrong pid");
}

static void t_gettid_no_ops(void) {
    linux_process_reset_for_tests();
    int64_t r = linux_gettid();
    TEST("gettid: no ops -> 0 (defensive, no crash)");
    if (r == 0) PASS(); else FAIL("expected 0");
}

static void t_install_null_clears_gettid_ops(void) {
    install_fake_ops();
    linux_process_install_ops(NULL);
    int64_t r = linux_gettid();
    TEST("process install_ops(NULL) clears gettid task accessors");
    if (r == 0) PASS(); else FAIL("task accessors not cleared");
}

/* ------------ sched_yield (S1.11) -------------------------------- */

static void t_sched_yield_calls_ops(void) {
    install_fake_ops();
    int64_t r = linux_sched_yield();
    TEST("sched_yield: returns 0 and calls yield cb");
    if (r == 0 && g_yield_count == 1) PASS(); else FAIL("yield cb not called");
}

static void t_sched_yield_no_ops(void) {
    linux_process_reset_for_tests();
    int64_t r = linux_sched_yield();
    TEST("sched_yield: no ops -> 0 (no crash on NULL yield)");
    if (r == 0) PASS(); else FAIL("expected 0");
}

static void t_install_null_clears_yield_ops(void) {
    install_fake_ops();
    linux_process_install_ops(NULL);
    int64_t r = linux_sched_yield();
    TEST("process install_ops(NULL) clears sched_yield callback");
    if (r == 0 && g_yield_count == 0) PASS();
    else FAIL("yield callback not cleared");
}

/* ------------ sched_getaffinity (S1.11) -------------------------- */

static void t_getaffinity_basic(void) {
    install_fake_ops();
    uint8_t mask[16] = {0xFF};
    int64_t r = linux_sched_getaffinity(0, sizeof(mask), mask);
    TEST("sched_getaffinity: returns 8, mask[0]=0x01, others 0");
    int ok = (r == 8 && mask[0] == 0x01);
    for (int i = 1; i < 16; i++) if (mask[i] != 0) ok = 0;
    if (ok) PASS(); else FAIL("mask shape wrong");
}

static void t_getaffinity_specific_pid(void) {
    install_fake_ops();
    uint8_t mask[8];
    int64_t r = linux_sched_getaffinity(100, sizeof(mask), mask);
    TEST("sched_getaffinity: pid=100 (other task) resolves OK");
    if (r == 8 && mask[0] == 0x01) PASS(); else FAIL("other pid failed");
}

static void t_getaffinity_unknown_pid(void) {
    install_fake_ops();
    uint8_t mask[8];
    int64_t r = linux_sched_getaffinity(99999, sizeof(mask), mask);
    TEST("sched_getaffinity: unknown pid -> -ESRCH");
    if (r == -LINUX_ESRCH) PASS(); else FAIL("expected -ESRCH");
}

static void t_install_null_clears_affinity_ops(void) {
    install_fake_ops();
    linux_process_install_ops(NULL);
    uint8_t mask[8];
    int64_t r = linux_sched_getaffinity(0, sizeof(mask), mask);
    TEST("process install_ops(NULL) clears affinity task accessors");
    if (r == -LINUX_ESRCH) PASS(); else FAIL("affinity accessors not cleared");
}

static void t_getaffinity_bad_size(void) {
    install_fake_ops();
    uint8_t mask[8];
    int64_t r1 = linux_sched_getaffinity(0, 0, mask);
    int64_t r2 = linux_sched_getaffinity(0, 7, mask); /* not 8-aligned */
    TEST("sched_getaffinity: bad cpusetsize -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL) PASS(); else FAIL("unchecked");
}

static void t_getaffinity_null_mask(void) {
    install_fake_ops();
    int64_t r = linux_sched_getaffinity(0, 8, NULL);
    TEST("sched_getaffinity: NULL mask -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("expected -EFAULT");
}

/* ------------ sched_setaffinity (S1.11) -------------------------- */

static void t_setaffinity_basic(void) {
    install_fake_ops();
    uint8_t mask[8] = {0x01};
    int64_t r = linux_sched_setaffinity(0, sizeof(mask), mask);
    TEST("sched_setaffinity: returns 0 for valid mask");
    if (r == 0) PASS(); else FAIL("expected 0");
}

static void t_setaffinity_empty_mask(void) {
    install_fake_ops();
    uint8_t mask[8] = {0};
    int64_t r = linux_sched_setaffinity(0, sizeof(mask), mask);
    TEST("sched_setaffinity: empty mask -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("expected -EINVAL");
}

static void t_setaffinity_unknown_pid(void) {
    install_fake_ops();
    uint8_t mask[8] = {0x01};
    int64_t r = linux_sched_setaffinity(99, sizeof(mask), mask);
    TEST("sched_setaffinity: unknown pid -> -ESRCH");
    if (r == -LINUX_ESRCH) PASS(); else FAIL("expected -ESRCH");
}

/* ------------ prctl (S1.9) --------------------------------------- */

static void t_prctl_set_name(void) {
    install_fake_ops();
    int64_t r = linux_prctl(LINUX_PR_SET_NAME, (uint64_t)(uintptr_t)"newname", 0, 0, 0);
    TEST("prctl PR_SET_NAME: renames current task");
    if (r == 0 && strcmp(g_fake_tasks[0].name, "newname") == 0) PASS();
    else FAIL("rename failed");
}

static void t_prctl_set_name_truncates_at_15(void) {
    install_fake_ops();
    /* 20-char string; Linux caps at 15 + NUL. */
    int64_t r = linux_prctl(LINUX_PR_SET_NAME,
                            (uint64_t)(uintptr_t)"012345678901234567890", 0, 0, 0);
    TEST("prctl PR_SET_NAME: truncates at 15 chars + NUL (Linux 16-byte cap)");
    if (r == 0 && strlen(g_fake_tasks[0].name) == 15 &&
        strcmp(g_fake_tasks[0].name, "012345678901234") == 0) PASS();
    else FAIL("truncation wrong");
}

static void t_prctl_get_name(void) {
    install_fake_ops();
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));
    int64_t r = linux_prctl(LINUX_PR_GET_NAME, (uint64_t)(uintptr_t)buf, 0, 0, 0);
    TEST("prctl PR_GET_NAME: writes 16 bytes NUL-padded with task name");
    if (r == 0 && strcmp(buf, "capysh") == 0 && buf[15] == '\0') PASS();
    else FAIL("get_name shape wrong");
}

static void t_prctl_set_name_null(void) {
    install_fake_ops();
    int64_t r = linux_prctl(LINUX_PR_SET_NAME, 0, 0, 0, 0);
    TEST("prctl PR_SET_NAME: NULL ptr -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("expected -EFAULT");
}

static void t_prctl_unknown_op(void) {
    install_fake_ops();
    int64_t r = linux_prctl(999, 0, 0, 0, 0);
    TEST("prctl: unknown op -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("expected -EINVAL");
}

/* ------------ prlimit64 (S1.17) ---------------------------------- */

static void t_prlimit_as(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(0, LINUX_RLIMIT_AS, NULL, &lim);
    TEST("prlimit64 AS: returns 8 GiB soft+hard");
    if (r == 0 && lim.rlim_cur == LINUX_CAPYOS_RLIMIT_AS_MAX &&
        lim.rlim_max == LINUX_CAPYOS_RLIMIT_AS_MAX) PASS();
    else FAIL("AS wrong");
}

static void t_prlimit_nofile(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(0, LINUX_RLIMIT_NOFILE, NULL, &lim);
    TEST("prlimit64 NOFILE: returns 1024 soft+hard");
    if (r == 0 && lim.rlim_cur == 1024 && lim.rlim_max == 1024) PASS();
    else FAIL("NOFILE wrong");
}

static void t_prlimit_stack(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(0, LINUX_RLIMIT_STACK, NULL, &lim);
    TEST("prlimit64 STACK: soft=8 MiB, hard=64 MiB");
    if (r == 0 && lim.rlim_cur == LINUX_CAPYOS_RLIMIT_STACK_SOFT &&
        lim.rlim_max == LINUX_CAPYOS_RLIMIT_STACK_HARD) PASS();
    else FAIL("STACK wrong");
}

static void t_prlimit_untracked_is_infinity(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(0, 0 /* RLIMIT_CPU */, NULL, &lim);
    TEST("prlimit64 untracked: returns RLIM_INFINITY");
    if (r == 0 && lim.rlim_cur == LINUX_RLIM_INFINITY &&
        lim.rlim_max == LINUX_RLIM_INFINITY) PASS();
    else FAIL("untracked not infinity");
}

static void t_prlimit_bad_resource(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(0, LINUX_RLIMIT_NLIMITS + 5, NULL, &lim);
    TEST("prlimit64 resource >= NLIMITS -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("expected -EINVAL");
}

static void t_prlimit_new_limit_eperm(void) {
    install_fake_ops();
    struct linux_rlimit64 new_lim = { 999, 999 };
    int64_t r = linux_prlimit64(0, LINUX_RLIMIT_NOFILE, &new_lim, NULL);
    TEST("prlimit64 with new_limit -> -EPERM (kernel policy fixed)");
    if (r == -LINUX_EPERM) PASS(); else FAIL("expected -EPERM");
}

static void t_prlimit_other_pid(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(100, LINUX_RLIMIT_AS, NULL, &lim);
    TEST("prlimit64 pid != current -> -EPERM (no CAP_SYS_RESOURCE)");
    if (r == -LINUX_EPERM) PASS(); else FAIL("expected -EPERM");
}

static void t_prlimit_self_pid_ok(void) {
    install_fake_ops();
    struct linux_rlimit64 lim;
    int64_t r = linux_prlimit64(42, LINUX_RLIMIT_AS, NULL, &lim);
    TEST("prlimit64 pid == current.pid -> OK");
    if (r == 0 && lim.rlim_cur == LINUX_CAPYOS_RLIMIT_AS_MAX) PASS();
    else FAIL("self by pid failed");
}

/* ------------ set_tid_address (S1.10) --------------------------- */

static void t_set_tid_address_returns_tid(void) {
    install_fake_ops();
    int64_t r = linux_set_tid_address(0xDEADBEEFull);
    TEST("set_tid_address: returns current tid (Linux semantics)");
    if (r == 42 && linux_process_test_get_tid_address() == 0xDEADBEEFull) PASS();
    else FAIL("tid not returned or pointer not stored");
}

static void t_set_tid_address_null_ptr_ok(void) {
    install_fake_ops();
    /* Linux accepts NULL: it just clears the stored ptr. Return is
     * still the current tid. */
    int64_t r = linux_set_tid_address(0);
    TEST("set_tid_address: NULL ptr accepted, returns tid, clears storage");
    if (r == 42 && linux_process_test_get_tid_address() == 0) PASS();
    else FAIL("NULL not handled");
}

/* ------------ set_robust_list (S1.10) --------------------------- */

static void t_set_robust_list_basic(void) {
    install_fake_ops();
    int64_t r = linux_set_robust_list(0xC0FFEE000ull, LINUX_ROBUST_LIST_HEAD_SIZE);
    TEST("set_robust_list: correct len stores head and returns 0");
    if (r == 0 &&
        linux_process_test_get_robust_list() == 0xC0FFEE000ull &&
        linux_process_test_get_robust_list_len() == 24u) PASS();
    else FAIL("storage wrong");
}

static void t_set_robust_list_bad_len(void) {
    install_fake_ops();
    int64_t r1 = linux_set_robust_list(0x1000ull, 23);
    int64_t r2 = linux_set_robust_list(0x1000ull, 25);
    int64_t r3 = linux_set_robust_list(0x1000ull, 0);
    TEST("set_robust_list: any len != 24 -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL && r3 == -LINUX_EINVAL)
        PASS();
    else FAIL("len validation wrong");
}

/* -------- musl bring-up syscalls (sessao 20) -------- */

static void t_getpid_matches_gettid(void) {
    install_fake_ops();
    int64_t a = linux_getpid();
    int64_t b = linux_gettid();
    TEST("getpid == gettid (single-thread per process today)");
    if (a == b && a > 0) PASS();
    else FAIL("not equal");
}

static void t_getppid_returns_init(void) {
    int64_t r = linux_getppid();
    TEST("getppid returns 1 (init's pid)");
    if (r == 1) PASS();
    else FAIL("not 1");
}

static void t_cred_all_zero(void) {
    int64_t a = linux_getuid();
    int64_t b = linux_geteuid();
    int64_t c = linux_getgid();
    int64_t d = linux_getegid();
    TEST("getuid/geteuid/getgid/getegid all return 0 (root)");
    if (a == 0 && b == 0 && c == 0 && d == 0) PASS();
    else FAIL("non-zero credentials");
}

static void t_uname_null_efault(void) {
    int64_t r = linux_uname(NULL);
    TEST("uname(NULL) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_uname_sysname_linux(void) {
    struct linux_utsname uts;
    /* Pre-trash the buffer to verify zero-padding works. */
    for (size_t i = 0; i < sizeof(uts); i++) ((char *)&uts)[i] = 0xAA;
    int64_t r = linux_uname(&uts);
    /* sysname must equal "Linux" (musl/glibc/JS shell key off this). */
    int sysname_ok = (uts.sysname[0] == 'L' && uts.sysname[1] == 'i' &&
                      uts.sysname[2] == 'n' && uts.sysname[3] == 'u' &&
                      uts.sysname[4] == 'x' && uts.sysname[5] == '\0');
    TEST("uname: sysname == 'Linux' (zero-terminated)");
    if (r == 0 && sysname_ok) PASS();
    else FAIL("sysname wrong");
}

static void t_uname_machine_x86_64(void) {
    struct linux_utsname uts = {0};
    linux_uname(&uts);
    int ok = (uts.machine[0] == 'x' && uts.machine[1] == '8' &&
              uts.machine[2] == '6' && uts.machine[3] == '_' &&
              uts.machine[4] == '6' && uts.machine[5] == '4' &&
              uts.machine[6] == '\0');
    TEST("uname: machine == 'x86_64'");
    if (ok) PASS();
    else FAIL("machine wrong");
}

static void t_uname_release_capyos(void) {
    struct linux_utsname uts = {0};
    linux_uname(&uts);
    /* Look for "capyos" inside release. */
    int found = 0;
    for (int i = 0; i + 5 < LINUX_UTSNAME_FIELD; i++) {
        if (uts.release[i] == 'c' && uts.release[i+1] == 'a' &&
            uts.release[i+2] == 'p' && uts.release[i+3] == 'y' &&
            uts.release[i+4] == 'o' && uts.release[i+5] == 's') {
            found = 1; break;
        }
    }
    TEST("uname: release contains 'capyos' substring");
    if (found) PASS();
    else FAIL("capyos not found in release");
}

static void t_uname_zero_pads_unused_bytes(void) {
    struct linux_utsname uts;
    /* Trash the buffer with non-zero pattern. */
    for (size_t i = 0; i < sizeof(uts); i++) ((char *)&uts)[i] = 0xAA;
    linux_uname(&uts);
    /* "Linux" is 5 chars + NUL = 6 bytes used; bytes 6..64 must be 0. */
    int padded = 1;
    for (int i = 6; i < LINUX_UTSNAME_FIELD; i++) {
        if (uts.sysname[i] != '\0') { padded = 0; break; }
    }
    TEST("uname: trailing bytes after string are zero-padded");
    if (padded) PASS();
    else FAIL("not zero-padded");
}

int test_linux_process_run(void) {
    printf("[test_linux_process]\n");
    tests_run = tests_passed = 0;

    t_gettid_returns_pid();
    t_gettid_no_ops();
    t_install_null_clears_gettid_ops();

    t_sched_yield_calls_ops();
    t_sched_yield_no_ops();
    t_install_null_clears_yield_ops();

    t_getaffinity_basic();
    t_getaffinity_specific_pid();
    t_getaffinity_unknown_pid();
    t_install_null_clears_affinity_ops();
    t_getaffinity_bad_size();
    t_getaffinity_null_mask();

    t_setaffinity_basic();
    t_setaffinity_empty_mask();
    t_setaffinity_unknown_pid();

    t_prctl_set_name();
    t_prctl_set_name_truncates_at_15();
    t_prctl_get_name();
    t_prctl_set_name_null();
    t_prctl_unknown_op();

    t_prlimit_as();
    t_prlimit_nofile();
    t_prlimit_stack();
    t_prlimit_untracked_is_infinity();
    t_prlimit_bad_resource();
    t_prlimit_new_limit_eperm();
    t_prlimit_other_pid();
    t_prlimit_self_pid_ok();

    t_set_tid_address_returns_tid();
    t_set_tid_address_null_ptr_ok();
    t_getpid_matches_gettid();
    t_getppid_returns_init();
    t_cred_all_zero();
    t_uname_null_efault();
    t_uname_sysname_linux();
    t_uname_machine_x86_64();
    t_uname_release_capyos();
    t_uname_zero_pads_unused_bytes();

    t_set_robust_list_basic();
    t_set_robust_list_bad_len();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
