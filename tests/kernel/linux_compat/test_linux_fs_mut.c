#include "kernel/linux_compat/linux_fs_mut.h"
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

/* --- ops fixture --- */

static int     g_mkdir_calls;
static int     g_rmdir_calls;
static int     g_unlink_calls;
static int     g_rename_calls;
static char    g_last_path[256];
static char    g_last_old[256];
static char    g_last_new[256];
static uint32_t g_last_mode;
static uint32_t g_last_flags;
static int64_t g_next_rc;

static void cap(char *dst, const char *src) {
    size_t i = 0;
    while (i < sizeof(g_last_path) - 1 && src && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int64_t fake_mkdir(const char *p, uint32_t m) {
    g_mkdir_calls++; cap(g_last_path, p); g_last_mode = m;
    return g_next_rc;
}
static int64_t fake_rmdir(const char *p) {
    g_rmdir_calls++; cap(g_last_path, p);
    return g_next_rc;
}
static int64_t fake_unlink(const char *p) {
    g_unlink_calls++; cap(g_last_path, p);
    return g_next_rc;
}
static int64_t fake_rename(const char *o, const char *n, uint32_t f) {
    g_rename_calls++; cap(g_last_old, o); cap(g_last_new, n); g_last_flags = f;
    return g_next_rc;
}

static void install_fake(void) {
    static const struct linux_fs_mut_ops o = {
        .mkdir  = fake_mkdir,
        .rmdir  = fake_rmdir,
        .unlink = fake_unlink,
        .rename = fake_rename,
    };
    g_mkdir_calls = g_rmdir_calls = g_unlink_calls = g_rename_calls = 0;
    g_last_path[0] = g_last_old[0] = g_last_new[0] = '\0';
    g_last_mode = g_last_flags = 0;
    g_next_rc = 0;
    linux_fs_mut_reset_for_tests();
    linux_fs_mut_install_ops(&o);
}

/* --- mkdir / mkdirat --- */

static void t_mkdir_null_efault(void) {
    install_fake();
    TEST("mkdir(NULL, 0755) -> -EFAULT");
    if (linux_mkdir(NULL, 0755) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_mkdir_empty_enoent(void) {
    install_fake();
    TEST("mkdir(\"\", 0755) -> -ENOENT");
    if (linux_mkdir("", 0755) == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_mkdir_no_ops_enosys(void) {
    linux_fs_mut_reset_for_tests();
    TEST("mkdir(\"/foo\", 0755) without ops -> -ENOSYS");
    if (linux_mkdir("/foo", 0755) == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_mkdir_calls_provider_with_clamped_mode(void) {
    install_fake();
    int64_t r = linux_mkdir("/foo", 0xFFFFFFFFu);
    TEST("mkdir clamps mode to 07777 before provider");
    if (r == 0 && g_mkdir_calls == 1 && g_last_mode == 07777 &&
        strcmp(g_last_path, "/foo") == 0) PASS();
    else FAIL("clamp/provider broken");
}

static void t_mkdirat_at_fdcwd_delegates(void) {
    install_fake();
    int64_t r = linux_mkdirat(LINUX_FS_MUT_AT_FDCWD, "/x", 0700);
    TEST("mkdirat(AT_FDCWD, ...) delegates to mkdir");
    if (r == 0 && g_mkdir_calls == 1) PASS();
    else FAIL("AT_FDCWD did not delegate");
}

static void t_mkdirat_other_fd_enotdir(void) {
    install_fake();
    TEST("mkdirat(7, ...) -> -ENOTDIR (no dir-fd table)");
    if (linux_mkdirat(7, "/x", 0700) == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

/* --- rmdir / unlink / unlinkat --- */

static void t_rmdir_null_efault(void) {
    install_fake();
    TEST("rmdir(NULL) -> -EFAULT");
    if (linux_rmdir(NULL) == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_unlink_no_ops_enosys(void) {
    linux_fs_mut_reset_for_tests();
    TEST("unlink(\"/foo\") without ops -> -ENOSYS");
    if (linux_unlink("/foo") == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_unlinkat_unknown_flags_einval(void) {
    install_fake();
    TEST("unlinkat(AT_FDCWD, \"/x\", 0xDEAD) -> -EINVAL");
    if (linux_unlinkat(LINUX_FS_MUT_AT_FDCWD, "/x", 0xDEAD)
        == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_unlinkat_removedir_routes_to_rmdir(void) {
    install_fake();
    int64_t r = linux_unlinkat(LINUX_FS_MUT_AT_FDCWD, "/x",
                               LINUX_AT_REMOVEDIR);
    TEST("unlinkat with AT_REMOVEDIR -> rmdir provider");
    if (r == 0 && g_rmdir_calls == 1 && g_unlink_calls == 0) PASS();
    else FAIL("did not route to rmdir");
}

static void t_unlinkat_no_flag_routes_to_unlink(void) {
    install_fake();
    int64_t r = linux_unlinkat(LINUX_FS_MUT_AT_FDCWD, "/x", 0);
    TEST("unlinkat without flags -> unlink provider");
    if (r == 0 && g_unlink_calls == 1 && g_rmdir_calls == 0) PASS();
    else FAIL("did not route to unlink");
}

static void t_unlinkat_other_fd_enotdir(void) {
    install_fake();
    TEST("unlinkat(7, ...) -> -ENOTDIR");
    if (linux_unlinkat(7, "/x", 0) == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

/* --- rename / renameat / renameat2 --- */

static void t_rename_calls_provider(void) {
    install_fake();
    int64_t r = linux_rename("/old", "/new");
    TEST("rename(\"/old\",\"/new\") -> provider with flags=0");
    if (r == 0 && g_rename_calls == 1 && g_last_flags == 0 &&
        strcmp(g_last_old, "/old") == 0 &&
        strcmp(g_last_new, "/new") == 0) PASS();
    else FAIL("provider not invoked");
}

static void t_rename_null_old_efault(void) {
    install_fake();
    TEST("rename(NULL, \"/new\") -> -EFAULT");
    if (linux_rename(NULL, "/new") == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_rename_empty_new_enoent(void) {
    install_fake();
    TEST("rename(\"/old\", \"\") -> -ENOENT");
    if (linux_rename("/old", "") == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_renameat2_unknown_flags_einval(void) {
    install_fake();
    TEST("renameat2 with unknown flag -> -EINVAL");
    int64_t r = linux_renameat2(LINUX_FS_MUT_AT_FDCWD, "/a",
                                LINUX_FS_MUT_AT_FDCWD, "/b", 0xDEAD);
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_renameat2_noreplace_and_exchange_einval(void) {
    install_fake();
    TEST("renameat2 with NOREPLACE|EXCHANGE -> -EINVAL");
    int64_t r = linux_renameat2(LINUX_FS_MUT_AT_FDCWD, "/a",
                                LINUX_FS_MUT_AT_FDCWD, "/b",
                                LINUX_RENAME_NOREPLACE |
                                LINUX_RENAME_EXCHANGE);
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_renameat2_known_flag_passes_through(void) {
    install_fake();
    int64_t r = linux_renameat2(LINUX_FS_MUT_AT_FDCWD, "/a",
                                LINUX_FS_MUT_AT_FDCWD, "/b",
                                LINUX_RENAME_NOREPLACE);
    TEST("renameat2 with NOREPLACE -> provider with flags forwarded");
    if (r == 0 && g_rename_calls == 1 &&
        g_last_flags == LINUX_RENAME_NOREPLACE) PASS();
    else FAIL("flags not forwarded");
}

static void t_renameat2_other_fd_enotdir(void) {
    install_fake();
    TEST("renameat2(7, ..., 8, ...) -> -ENOTDIR");
    int64_t r = linux_renameat2(7, "/a", 8, "/b", 0);
    if (r == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_provider_error_forwarded(void) {
    install_fake();
    g_next_rc = -LINUX_EEXIST;
    int64_t r = linux_mkdir("/x", 0700);
    TEST("provider rc forwarded verbatim (-EEXIST)");
    if (r == -LINUX_EEXIST) PASS();
    else FAIL("rc not forwarded");
}

static void t_install_null_clears(void) {
    install_fake();
    linux_fs_mut_install_ops(NULL);
    int64_t r1 = linux_mkdir("/x", 0700);
    int64_t r2 = linux_rmdir("/x");
    int64_t r3 = linux_unlink("/x");
    int64_t r4 = linux_rename("/old", "/new");
    TEST("fs_mut install_ops(NULL) clears mutation callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        r3 == -LINUX_ENOSYS && r4 == -LINUX_ENOSYS &&
        g_mkdir_calls == 0 && g_rmdir_calls == 0 &&
        g_unlink_calls == 0 && g_rename_calls == 0) PASS();
    else FAIL("install(NULL) didn't clear");
}

static void t_reset_clears(void) {
    install_fake();
    linux_fs_mut_reset_for_tests();
    int64_t r1 = linux_mkdir("/x", 0700);
    int64_t r2 = linux_rmdir("/x");
    int64_t r3 = linux_unlink("/x");
    int64_t r4 = linux_rename("/old", "/new");
    TEST("fs_mut reset clears installed callbacks");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS &&
        r3 == -LINUX_ENOSYS && r4 == -LINUX_ENOSYS &&
        g_mkdir_calls == 0 && g_rmdir_calls == 0 &&
        g_unlink_calls == 0 && g_rename_calls == 0) PASS();
    else FAIL("reset didn't clear");
}

int test_linux_fs_mut_run(void) {
    printf("[test_linux_fs_mut]\n");
    tests_run = tests_passed = 0;

    t_mkdir_null_efault();
    t_mkdir_empty_enoent();
    t_mkdir_no_ops_enosys();
    t_mkdir_calls_provider_with_clamped_mode();
    t_mkdirat_at_fdcwd_delegates();
    t_mkdirat_other_fd_enotdir();

    t_rmdir_null_efault();
    t_unlink_no_ops_enosys();
    t_unlinkat_unknown_flags_einval();
    t_unlinkat_removedir_routes_to_rmdir();
    t_unlinkat_no_flag_routes_to_unlink();
    t_unlinkat_other_fd_enotdir();

    t_rename_calls_provider();
    t_rename_null_old_efault();
    t_rename_empty_new_enoent();
    t_renameat2_unknown_flags_einval();
    t_renameat2_noreplace_and_exchange_einval();
    t_renameat2_known_flag_passes_through();
    t_renameat2_other_fd_enotdir();

    t_provider_error_forwarded();
    t_install_null_clears();
    t_reset_clears();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
