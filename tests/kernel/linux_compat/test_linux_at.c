#include "kernel/linux_compat/linux_at.h"
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

/* --- access --- */

static void t_access_null_efault(void) {
    TEST("access(NULL, F_OK) -> -EFAULT");
    if (linux_access(NULL, LINUX_AT_F_OK) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_access_empty_enoent(void) {
    TEST("access(\"\", F_OK) -> -ENOENT");
    if (linux_access("", LINUX_AT_F_OK) == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_access_invalid_mode_einval(void) {
    TEST("access(\"/\", 0x100) -> -EINVAL (unknown mode bit)");
    if (linux_access("/", 0x100) == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_access_root_f_ok(void) {
    TEST("access(\"/\", F_OK) -> 0");
    if (linux_access("/", LINUX_AT_F_OK) == 0) PASS();
    else FAIL("root not recognised");
}

static void t_access_dev_null_rwx(void) {
    int mode = LINUX_AT_R_OK | LINUX_AT_W_OK | LINUX_AT_X_OK;
    TEST("access(\"/dev/null\", R|W|X) -> 0");
    if (linux_access("/dev/null", mode) == 0) PASS();
    else FAIL("dev/null not recognised");
}

static void t_access_proc_status_f_ok(void) {
    TEST("access(\"/proc/self/status\", F_OK) -> 0");
    if (linux_access("/proc/self/status", LINUX_AT_F_OK) == 0) PASS();
    else FAIL("proc status not recognised");
}

static void t_access_tmp_x_ok(void) {
    TEST("access(\"/tmp\", X_OK) -> 0");
    if (linux_access("/tmp", LINUX_AT_X_OK) == 0) PASS();
    else FAIL("tmp not recognised");
}

static void t_access_unknown_enoent(void) {
    TEST("access(\"/etc/hosts\", F_OK) -> -ENOENT");
    if (linux_access("/etc/hosts", LINUX_AT_F_OK) == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT for unknown not surfaced");
}

/* --- faccessat --- */

static void t_faccessat_at_fdcwd_root_ok(void) {
    TEST("faccessat(AT_FDCWD, \"/\", F_OK, 0) -> 0");
    if (linux_faccessat(LINUX_AT_FDCWD, "/", LINUX_AT_F_OK, 0) == 0) PASS();
    else FAIL("AT_FDCWD root failed");
}

static void t_faccessat_at_fdcwd_proc_maps_ok(void) {
    TEST("faccessat(AT_FDCWD, \"/proc/self/maps\", F_OK, 0) -> 0");
    if (linux_faccessat(LINUX_AT_FDCWD, "/proc/self/maps",
                        LINUX_AT_F_OK, 0) == 0) PASS();
    else FAIL("AT_FDCWD proc maps failed");
}

static void t_faccessat_at_fdcwd_dev_shm_ok(void) {
    TEST("faccessat(AT_FDCWD, \"/dev/shm\", X_OK, 0) -> 0");
    if (linux_faccessat(LINUX_AT_FDCWD, "/dev/shm",
                        LINUX_AT_X_OK, 0) == 0) PASS();
    else FAIL("AT_FDCWD dev shm failed");
}

static void t_faccessat_unknown_flag_einval(void) {
    TEST("faccessat(..., flags=0x10000) -> -EINVAL");
    if (linux_faccessat(LINUX_AT_FDCWD, "/", LINUX_AT_F_OK, 0x10000)
        == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_faccessat_dirfd_with_path_enotdir(void) {
    TEST("faccessat(dirfd=3, \"foo\", F_OK, 0) -> -ENOTDIR");
    if (linux_faccessat(3, "foo", LINUX_AT_F_OK, 0)
        == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_faccessat_empty_path_with_at_empty(void) {
    TEST("faccessat(2, \"\", F_OK, AT_EMPTY_PATH) -> 0 (probe fd)");
    if (linux_faccessat(2, "", LINUX_AT_F_OK, LINUX_AT_EMPTY_PATH)
        == 0) PASS();
    else FAIL("AT_EMPTY_PATH dirfd probe failed");
}

static void t_faccessat_empty_no_at_empty_enoent(void) {
    TEST("faccessat(AT_FDCWD, \"\", F_OK, 0) -> -ENOENT");
    if (linux_faccessat(LINUX_AT_FDCWD, "", LINUX_AT_F_OK, 0)
        == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced for empty without AT_EMPTY_PATH");
}

/* --- fstatat --- */

static void t_fstatat_null_buf_efault(void) {
    TEST("fstatat(0, \"\", NULL, AT_EMPTY_PATH) -> -EFAULT");
    if (linux_fstatat(0, "", NULL, LINUX_AT_EMPTY_PATH)
        == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_fstatat_empty_path_at_empty_projects_fstat(void) {
    struct linux_stat sb;
    int64_t r = linux_fstatat(0, "", &sb, LINUX_AT_EMPTY_PATH);
    TEST("fstatat(0, \"\", AT_EMPTY_PATH) -> S_IFCHR via fstat");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFCHR) PASS();
    else FAIL("fstat projection wrong");
}

static void t_fstatat_empty_path_no_flag_tolerated(void) {
    struct linux_stat sb;
    int64_t r = linux_fstatat(7, "", &sb, 0);
    TEST("fstatat(7, \"\", flags=0) tolerated as fstat-on-fd");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFREG) PASS();
    else FAIL("empty path without AT_EMPTY_PATH not tolerated");
}

static void t_fstatat_negative_dirfd_empty_ebadf(void) {
    struct linux_stat sb;
    TEST("fstatat(-1, \"\", AT_EMPTY_PATH, &sb) -> -EBADF");
    if (linux_fstatat(-1, "", &sb, LINUX_AT_EMPTY_PATH)
        == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_fstatat_at_fdcwd_unknown_path_enosys(void) {
    struct linux_stat sb;
    TEST("fstatat(AT_FDCWD, unknown path, &sb, 0) -> -ENOSYS");
    if (linux_fstatat(LINUX_AT_FDCWD, "/etc/hosts", &sb, 0)
        == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_fstatat_at_fdcwd_known_path_dir(void) {
    struct linux_stat sb;
    int64_t r = linux_fstatat(LINUX_AT_FDCWD, "/proc", &sb, 0);
    TEST("fstatat(AT_FDCWD, \"/proc\", &sb, 0) -> S_IFDIR");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFDIR) PASS();
    else FAIL("known path not projected through stat");
}

static void t_fstatat_nofollow_proc_self_exe_link(void) {
    struct linux_stat sb;
    int64_t r = linux_fstatat(LINUX_AT_FDCWD, "/proc/self/exe", &sb,
                              LINUX_AT_SYMLINK_NOFOLLOW);
    TEST("fstatat(AT_FDCWD, /proc/self/exe, NOFOLLOW) -> S_IFLNK");
    if (r == 0 && (sb.st_mode & LINUX_S_IFMT) == LINUX_S_IFLNK) PASS();
    else FAIL("NOFOLLOW did not use lstat");
}

static void t_fstatat_dirfd_with_path_enotdir(void) {
    struct linux_stat sb;
    TEST("fstatat(3, \"rel\", &sb, 0) -> -ENOTDIR");
    if (linux_fstatat(3, "rel", &sb, 0) == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_fstatat_invalid_flags_einval(void) {
    struct linux_stat sb;
    TEST("fstatat(..., flags=0x80000) -> -EINVAL");
    if (linux_fstatat(LINUX_AT_FDCWD, "/", &sb, 0x80000)
        == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

int test_linux_at_run(void) {
    printf("[test_linux_at]\n");
    tests_run = tests_passed = 0;

    t_access_null_efault();
    t_access_empty_enoent();
    t_access_invalid_mode_einval();
    t_access_root_f_ok();
    t_access_dev_null_rwx();
    t_access_proc_status_f_ok();
    t_access_tmp_x_ok();
    t_access_unknown_enoent();

    t_faccessat_at_fdcwd_root_ok();
    t_faccessat_at_fdcwd_proc_maps_ok();
    t_faccessat_at_fdcwd_dev_shm_ok();
    t_faccessat_unknown_flag_einval();
    t_faccessat_dirfd_with_path_enotdir();
    t_faccessat_empty_path_with_at_empty();
    t_faccessat_empty_no_at_empty_enoent();

    t_fstatat_null_buf_efault();
    t_fstatat_empty_path_at_empty_projects_fstat();
    t_fstatat_empty_path_no_flag_tolerated();
    t_fstatat_negative_dirfd_empty_ebadf();
    t_fstatat_at_fdcwd_unknown_path_enosys();
    t_fstatat_at_fdcwd_known_path_dir();
    t_fstatat_nofollow_proc_self_exe_link();
    t_fstatat_dirfd_with_path_enotdir();
    t_fstatat_invalid_flags_einval();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
