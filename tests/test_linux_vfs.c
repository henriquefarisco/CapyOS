/* Host tests for linux_vfs (open/close/read/write/lseek). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake ops. */
static int g_open_calls;
static int g_close_calls;
static int64_t g_read_calls;
static int64_t g_write_calls;
static int64_t g_lseek_calls;
static int g_open_should_fail;
static int g_close_should_fail;

static int fake_open(const char *path, uint32_t flags, uint32_t mode) {
    (void)path; (void)flags; (void)mode;
    g_open_calls++;
    if (g_open_should_fail) return -LINUX_ENOENT;
    return 7;
}
static int fake_close(int fd) {
    (void)fd;
    g_close_calls++;
    return g_close_should_fail ? -LINUX_EBADF : 0;
}
static int64_t fake_read(int fd, void *buf, size_t len) {
    (void)fd; (void)buf;
    g_read_calls++;
    return (int64_t)len;
}
static int64_t fake_write(int fd, const void *buf, size_t len) {
    (void)fd; (void)buf;
    g_write_calls++;
    return (int64_t)len;
}
static int64_t fake_lseek(int fd, int64_t offset, int whence) {
    (void)fd; (void)whence;
    g_lseek_calls++;
    return offset;
}

static void install_fake(void) {
    linux_vfs_reset_for_tests();
    g_open_calls = g_close_calls = 0;
    g_read_calls = g_write_calls = g_lseek_calls = 0;
    g_open_should_fail = 0;
    g_close_should_fail = 0;
    static const struct linux_vfs_ops ops = {
        .open  = fake_open,
        .close = fake_close,
        .read  = fake_read,
        .write = fake_write,
        .lseek = fake_lseek,
    };
    linux_vfs_install_ops(&ops);
}

/* -------- open -------- */

static void t_open_basic(void) {
    install_fake();
    const char *p = "/etc/foo";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_RDONLY, 0);
    TEST("open: basic call returns op result, calls fake once");
    if (r == 7 && g_open_calls == 1) PASS(); else FAIL("basic call wrong");
}

static void t_open_null_path(void) {
    install_fake();
    int64_t r = linux_vfs_open(0, LINUX_VFS_O_RDONLY, 0);
    TEST("open: NULL path -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("NULL not rejected");
}

static void t_open_unknown_flag(void) {
    install_fake();
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, 0x10000000u, 0);
    TEST("open: unknown flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("unknown flag accepted");
}

static void t_open_excl_without_creat(void) {
    install_fake();
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_EXCL, 0);
    TEST("open: O_EXCL without O_CREAT -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("invariant not enforced");
}

static void t_open_bad_access_mode(void) {
    install_fake();
    const char *p = "/x";
    /* O_ACCMODE == 3 is the value of O_RDWR but is never the
     * valid pattern itself when bit 0 and 1 are both set
     * AS the value 3 is RDWR... actually in Linux, O_RDWR == 2.
     * The "all bits set" pattern (3) is reserved. */
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_ACCMODE, 0);
    TEST("open: access-mode == ACCMODE (all bits) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("bad access mode accepted");
}

static void t_open_no_ops_enosys(void) {
    linux_vfs_reset_for_tests();
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_RDONLY, 0);
    TEST("open: no ops -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("expected ENOSYS");
}

static void t_open_callback_failure(void) {
    install_fake();
    g_open_should_fail = 1;
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_RDONLY, 0);
    TEST("open: callback returns -ENOENT -> passes through to caller");
    if (r == -LINUX_ENOENT) PASS(); else FAIL("ENOENT not surfaced");
}

static int fake_open_efault(const char *p, uint32_t f, uint32_t m) {
    (void)p; (void)f; (void)m;
    return -LINUX_EFAULT;
}

static void t_open_callback_passes_other_errnos(void) {
    /* New contract: callback returns -LINUX_E*; the shim does NOT
     * remap to ENOENT. This is what the linux_vfs_router relies on
     * to surface EMFILE / EINVAL / EFAULT distinctly. */
    linux_vfs_reset_for_tests();
    static const struct linux_vfs_ops ops = { .open = fake_open_efault };
    linux_vfs_install_ops(&ops);
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_RDONLY, 0);
    TEST("open: shim preserves callback EFAULT (no ENOENT remap)");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("errno not preserved");
}

static void t_install_null_clears_open_ops(void) {
    install_fake();
    linux_vfs_install_ops(NULL);
    const char *p = "/x";
    int64_t r = linux_vfs_open((uint64_t)(uintptr_t)p, LINUX_VFS_O_RDONLY, 0);
    TEST("vfs install_ops(NULL) clears open callback");
    if (r == -LINUX_ENOSYS && g_open_calls == 0) PASS();
    else FAIL("open callback not cleared");
}

/* -------- close -------- */

static void t_close_basic(void) {
    install_fake();
    int64_t r = linux_vfs_close(7);
    TEST("close: returns 0 on valid fd");
    if (r == 0 && g_close_calls == 1) PASS(); else FAIL("close failed");
}

static void t_close_negative(void) {
    install_fake();
    int64_t r = linux_vfs_close(-1);
    TEST("close: negative fd -> -EBADF");
    if (r == -LINUX_EBADF && g_close_calls == 0) PASS();
    else FAIL("negative not rejected");
}

static void t_install_null_clears_close_ops(void) {
    install_fake();
    linux_vfs_install_ops(NULL);
    int64_t r = linux_vfs_close(7);
    TEST("vfs install_ops(NULL) clears close callback");
    if (r == -LINUX_ENOSYS && g_close_calls == 0) PASS();
    else FAIL("close callback not cleared");
}

/* -------- read/write -------- */

static void t_read_basic(void) {
    install_fake();
    char buf[8];
    int64_t r = linux_vfs_read(7, (uint64_t)(uintptr_t)buf, 8);
    TEST("read: returns op result; len > 0 + non-NULL buf -> calls op");
    if (r == 8 && g_read_calls == 1) PASS(); else FAIL("read wrong");
}

static void t_read_zero_len(void) {
    install_fake();
    int64_t r = linux_vfs_read(7, 0, 0);
    TEST("read: len=0 returns 0 even with NULL buf");
    if (r == 0 && g_read_calls == 0) PASS(); else FAIL("zero-len wrong");
}

static void t_read_null_buf(void) {
    install_fake();
    int64_t r = linux_vfs_read(7, 0, 16);
    TEST("read: NULL buf with len > 0 -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("NULL not rejected");
}

static void t_write_basic(void) {
    install_fake();
    char buf[4];
    int64_t r = linux_vfs_write(7, (uint64_t)(uintptr_t)buf, 4);
    TEST("write: returns op result");
    if (r == 4 && g_write_calls == 1) PASS(); else FAIL("write wrong");
}

static void t_write_negative_fd(void) {
    install_fake();
    int64_t r = linux_vfs_write(-3, 0x1000, 4);
    TEST("write: negative fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS(); else FAIL("EBADF not surfaced");
}

static void t_install_null_clears_rw_ops(void) {
    install_fake();
    linux_vfs_install_ops(NULL);
    char buf[8];
    int64_t r = linux_vfs_read(7, (uint64_t)(uintptr_t)buf, sizeof(buf));
    int64_t w = linux_vfs_write(7, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("vfs install_ops(NULL) clears read/write callbacks");
    if (r == -LINUX_ENOSYS && w == -LINUX_ENOSYS &&
        g_read_calls == 0 && g_write_calls == 0) PASS();
    else FAIL("read/write callbacks not cleared");
}

/* -------- lseek -------- */

static void t_lseek_basic(void) {
    install_fake();
    int64_t r = linux_vfs_lseek(7, 100, LINUX_SEEK_SET);
    TEST("lseek: SEEK_SET 100 returns op result (100)");
    if (r == 100 && g_lseek_calls == 1) PASS(); else FAIL("lseek wrong");
}

static void t_lseek_unknown_whence(void) {
    install_fake();
    int64_t r = linux_vfs_lseek(7, 0, 99);
    TEST("lseek: unknown whence -> -EINVAL");
    if (r == -LINUX_EINVAL && g_lseek_calls == 0) PASS();
    else FAIL("whence not validated");
}

static void t_lseek_negative_fd(void) {
    install_fake();
    int64_t r = linux_vfs_lseek(-1, 0, LINUX_SEEK_CUR);
    TEST("lseek: negative fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS(); else FAIL("EBADF not surfaced");
}

static void t_install_null_clears_lseek_ops(void) {
    install_fake();
    linux_vfs_install_ops(NULL);
    int64_t r = linux_vfs_lseek(7, 0, LINUX_SEEK_SET);
    TEST("vfs install_ops(NULL) clears lseek callback");
    if (r == -LINUX_ENOSYS && g_lseek_calls == 0) PASS();
    else FAIL("lseek callback not cleared");
}

/* -------- openat (sessao 20) -------- */

static void t_openat_at_fdcwd_dispatches(void) {
    install_fake();
    const char *p = "/x";
    int64_t r = linux_vfs_openat(LINUX_AT_FDCWD,
                                 (uint64_t)(uintptr_t)p,
                                 LINUX_VFS_O_RDONLY, 0);
    TEST("openat(AT_FDCWD, ...) dispatches to linux_vfs_open");
    if (r == 7 && g_open_calls == 1) PASS();
    else FAIL("AT_FDCWD path wrong");
}

static void t_openat_positive_dirfd_enotdir(void) {
    install_fake();
    int64_t r = linux_vfs_openat(7, 0x1000,
                                 LINUX_VFS_O_RDONLY, 0);
    TEST("openat: positive dirfd (no dir support) -> -ENOTDIR");
    if (r == -LINUX_ENOTDIR) PASS();
    else FAIL("ENOTDIR not surfaced");
}

static void t_openat_negative_dirfd_ebadf(void) {
    install_fake();
    /* Anything < 0 except AT_FDCWD is -EBADF. */
    int64_t r = linux_vfs_openat(-5, 0x1000,
                                 LINUX_VFS_O_RDONLY, 0);
    TEST("openat: negative dirfd (not AT_FDCWD) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

int test_linux_vfs_run(void) {
    printf("[test_linux_vfs]\n");
    tests_run = tests_passed = 0;

    t_open_basic();
    t_open_null_path();
    t_open_unknown_flag();
    t_open_excl_without_creat();
    t_open_bad_access_mode();
    t_open_no_ops_enosys();
    t_open_callback_failure();
    t_open_callback_passes_other_errnos();
    t_install_null_clears_open_ops();

    t_close_basic();
    t_close_negative();
    t_install_null_clears_close_ops();

    t_read_basic();
    t_read_zero_len();
    t_read_null_buf();
    t_write_basic();
    t_write_negative_fd();
    t_install_null_clears_rw_ops();

    t_lseek_basic();
    t_lseek_unknown_whence();
    t_lseek_negative_fd();
    t_install_null_clears_lseek_ops();

    t_openat_at_fdcwd_dispatches();
    t_openat_positive_dirfd_enotdir();
    t_openat_negative_dirfd_ebadf();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
