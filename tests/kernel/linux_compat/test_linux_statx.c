#include "kernel/linux_compat/linux_statx.h"
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

static void t_statx_null_buf_efault(void) {
    int64_t r = linux_statx(0, "", LINUX_STATX_AT_EMPTY_PATH,
                            LINUX_STATX_BASIC_STATS, NULL);
    TEST("statx(buf=NULL) -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_statx_fstat_mode_stdin_chr(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(0, "", LINUX_STATX_AT_EMPTY_PATH,
                            LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(0, \"\", AT_EMPTY_PATH) projects fstat -> S_IFCHR");
    if (r == 0 &&
        (sb.stx_mode & LINUX_S_IFMT) == LINUX_S_IFCHR &&
        sb.stx_blksize == 4096 &&
        sb.stx_nlink == 1) PASS();
    else FAIL("fstat projection wrong");
}

static void t_statx_fstat_mode_other_fd_reg(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(7, "", LINUX_STATX_AT_EMPTY_PATH,
                            LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(7, \"\") -> S_IFREG synthetic");
    if (r == 0 && (sb.stx_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("S_IFREG wrong");
}

static void t_statx_empty_path_no_at_flag_still_fstats(void) {
    /* Some musl paths issue statx(fd, "", 0, mask, &sb) without
     * AT_EMPTY_PATH; tolerate. */
    struct linux_statx sb;
    int64_t r = linux_statx(2, "", 0, LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(fd, \"\", flags=0) tolerated as fstat-on-fd");
    if (r == 0 && (sb.stx_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("empty path without AT_EMPTY_PATH didn't fstat");
}

static void t_statx_negative_dirfd_with_empty_ebadf(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(-1, "", LINUX_STATX_AT_EMPTY_PATH,
                            LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(dirfd=-1, \"\", AT_EMPTY_PATH) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_statx_path_at_fdcwd_enosys(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(LINUX_STATX_AT_FDCWD, "/etc/hosts",
                            0, LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(AT_FDCWD, unknown path) -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_statx_known_path_projects_stat(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(LINUX_STATX_AT_FDCWD, "/dev/null",
                            0, LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(AT_FDCWD, /dev/null) projects path stat -> S_IFCHR");
    if (r == 0 &&
        (sb.stx_mode & LINUX_S_IFMT) == LINUX_S_IFCHR &&
        sb.stx_blksize == 4096 &&
        sb.stx_nlink == 1) PASS();
    else FAIL("path stat projection wrong");
}

static void t_statx_nofollow_proc_self_exe_projects_lstat(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(LINUX_STATX_AT_FDCWD, "/proc/self/exe",
                            LINUX_STATX_AT_SYMLINK_NOFOLLOW,
                            LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(AT_FDCWD, /proc/self/exe, NOFOLLOW) -> S_IFLNK");
    if (r == 0 && (sb.stx_mode & LINUX_S_IFMT) == LINUX_S_IFLNK) PASS();
    else FAIL("NOFOLLOW did not project lstat");
}

static void t_statx_path_with_dirfd_enotdir(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(3, "rel/path", 0,
                            LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx(dirfd=3, \"rel/path\", ...) -> -ENOTDIR");
    if (r == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_statx_mask_clamped_to_supported(void) {
    struct linux_statx sb;
    int64_t r = linux_statx(0, "", LINUX_STATX_AT_EMPTY_PATH,
                            LINUX_STATX_BASIC_STATS, &sb);
    /* stx_mask should report only the supported subset. */
    TEST("statx: stx_mask clamped to LINUX_STATX_SUPPORTED");
    if (r == 0 &&
        (sb.stx_mask & ~LINUX_STATX_SUPPORTED) == 0 &&
        (sb.stx_mask & LINUX_STATX_MODE)) PASS();
    else FAIL("mask clamp wrong");
}

static void t_statx_zero_initialises_buffer(void) {
    struct linux_statx sb;
    uint8_t *p = (uint8_t *)&sb;
    for (size_t i = 0; i < sizeof(sb); i++) p[i] = 0xCD;
    (void)linux_statx(0, "", LINUX_STATX_AT_EMPTY_PATH,
                      LINUX_STATX_BASIC_STATS, &sb);
    TEST("statx: zeroes attributes_mask + reserved fields");
    if (sb.stx_attributes_mask == 0 && sb.stx_atime.tv_sec == 0 &&
        sb.stx_btime.tv_sec == 0) PASS();
    else FAIL("buffer not zeroed");
}

static void t_statx_sizeof_struct_256(void) {
    /* Document the struct size matches Linux ABI. */
    TEST("sizeof(struct linux_statx) == 256 (Linux ABI)");
    if (sizeof(struct linux_statx) == 256) PASS();
    else FAIL("size mismatch");
}

int test_linux_statx_run(void) {
    printf("[test_linux_statx]\n");
    tests_run = tests_passed = 0;

    t_statx_null_buf_efault();
    t_statx_fstat_mode_stdin_chr();
    t_statx_fstat_mode_other_fd_reg();
    t_statx_empty_path_no_at_flag_still_fstats();
    t_statx_negative_dirfd_with_empty_ebadf();
    t_statx_path_at_fdcwd_enosys();
    t_statx_known_path_projects_stat();
    t_statx_nofollow_proc_self_exe_projects_lstat();
    t_statx_path_with_dirfd_enotdir();
    t_statx_mask_clamped_to_supported();
    t_statx_zero_initialises_buffer();
    t_statx_sizeof_struct_256();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
