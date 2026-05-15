#include "kernel/linux_compat/linux_stat.h"
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

static void t_fstat_negative_fd_ebadf(void) {
    struct linux_stat sb;
    int64_t r = linux_fstat(-1, &sb);
    TEST("fstat(-1) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_fstat_null_out_efault(void) {
    int64_t r = linux_fstat(3, NULL);
    TEST("fstat(*, NULL) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_fstat_stdin_chr(void) {
    struct linux_stat sb = {.st_mode = 0xDEADBEEF};
    int64_t r = linux_fstat(0, &sb);
    /* mode high bits should be S_IFCHR. */
    TEST("fstat(0=stdin) reports S_IFCHR");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("stdin not IFCHR");
}

static void t_fstat_stdout_chr(void) {
    struct linux_stat sb;
    linux_fstat(1, &sb);
    TEST("fstat(1=stdout) reports S_IFCHR");
    if ((sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("stdout not IFCHR");
}

static void t_fstat_stderr_chr(void) {
    struct linux_stat sb;
    linux_fstat(2, &sb);
    TEST("fstat(2=stderr) reports S_IFCHR");
    if ((sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("stderr not IFCHR");
}

static void t_fstat_other_fd_reg(void) {
    struct linux_stat sb;
    linux_fstat(3, &sb);
    TEST("fstat(3) reports S_IFREG (regular file default)");
    if ((sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("non-stdio fd not IFREG");
}

static void t_fstat_high_fd_reg(void) {
    struct linux_stat sb;
    /* Pseudo-fs encoded fd e.g. devfs 0x8005. */
    linux_fstat(0x8005, &sb);
    TEST("fstat(devfs-encoded fd) reports S_IFREG (synthetic)");
    if ((sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("high fd not IFREG");
}

static void t_fstat_zeroes_unused(void) {
    struct linux_stat sb;
    /* Pre-trash the buffer to verify zeroing. */
    uint8_t *p = (uint8_t *)&sb;
    for (size_t i = 0; i < sizeof(sb); i++) p[i] = 0xAA;
    linux_fstat(3, &sb);
    /* After fstat, st_dev must be 0 (we don't track device IDs). */
    TEST("fstat: zero-initialises unused fields (st_dev=0)");
    if (sb.st_dev == 0 && sb.st_ino == 0) PASS();
    else FAIL("unused fields not cleared");
}

static void t_fstat_default_perms(void) {
    struct linux_stat sb;
    linux_fstat(3, &sb);
    /* Should have at least user-read permission. */
    TEST("fstat: synthetic mode has at least S_IRUSR");
    if (sb.st_mode & LINUX_S_IRUSR) PASS();
    else FAIL("default perms missing IRUSR");
}

static void t_fstat_nlink_one(void) {
    struct linux_stat sb;
    linux_fstat(3, &sb);
    TEST("fstat: st_nlink defaults to 1");
    if (sb.st_nlink == 1) PASS();
    else FAIL("nlink not 1");
}

static void t_stat_root_reports_dir(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("/", &sb);
    TEST("stat(\"/\") reports S_IFDIR");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFDIR &&
        (sb.st_mode & LINUX_S_IXUSR)) PASS();
    else FAIL("root not reported as directory");
}

static void t_stat_dev_null_reports_chr(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("/dev/null", &sb);
    TEST("stat(\"/dev/null\") reports S_IFCHR");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("dev null not reported as char device");
}

static void t_stat_proc_status_reports_reg(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("/proc/self/status", &sb);
    TEST("stat(\"/proc/self/status\") reports S_IFREG");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("proc status not reported as regular");
}

static void t_stat_unknown_path_returns_enosys(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("/etc/hosts", &sb);
    TEST("stat(unknown path) -> -ENOSYS for open+fstat fallback");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("unknown path did not preserve ENOSYS fallback");
}

static void t_lstat_proc_self_exe_reports_link(void) {
    struct linux_stat sb;
    int64_t r = linux_lstat("/proc/self/exe", &sb);
    TEST("lstat(\"/proc/self/exe\") reports S_IFLNK");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFLNK) PASS();
    else FAIL("proc self exe lstat not symlink");
}

static void t_stat_proc_self_exe_follows_to_reg(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("/proc/self/exe", &sb);
    TEST("stat(\"/proc/self/exe\") follows synthetic link to S_IFREG");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("proc self exe stat did not follow link");
}

static void t_stat_empty_path_enoent(void) {
    struct linux_stat sb;
    int64_t r = linux_stat("", &sb);
    TEST("stat(\"\") -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("empty path not ENOENT");
}

static void t_fstat_size_default(void) {
    struct linux_stat sb;
    linux_fstat(3, &sb);
    TEST("fstat: st_size defaults to 0 (unknown size)");
    if (sb.st_size == 0) PASS();
    else FAIL("size not zero");
}

static void t_fstat_blksize_4k(void) {
    struct linux_stat sb;
    linux_fstat(3, &sb);
    TEST("fstat: st_blksize defaults to 4096");
    if (sb.st_blksize == 4096) PASS();
    else FAIL("blksize not 4096");
}

int test_linux_stat_run(void) {
    printf("[test_linux_stat]\n");
    tests_run = tests_passed = 0;

    t_fstat_negative_fd_ebadf();
    t_fstat_null_out_efault();
    t_fstat_stdin_chr();
    t_fstat_stdout_chr();
    t_fstat_stderr_chr();
    t_fstat_other_fd_reg();
    t_fstat_high_fd_reg();
    t_fstat_zeroes_unused();
    t_fstat_default_perms();
    t_fstat_nlink_one();
    t_fstat_size_default();
    t_fstat_blksize_4k();
    t_stat_root_reports_dir();
    t_stat_dev_null_reports_chr();
    t_stat_proc_status_reports_reg();
    t_stat_unknown_path_returns_enosys();
    t_lstat_proc_self_exe_reports_link();
    t_stat_proc_self_exe_follows_to_reg();
    t_stat_empty_path_enoent();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
