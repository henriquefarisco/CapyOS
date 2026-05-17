/* Host tests for linux_tmpfs (S2.9). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_tmpfs.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

#define O_RDONLY    0x0000u
#define O_WRONLY    0x0001u
#define O_RDWR      0x0002u
#define O_CREAT     0x0040u
#define O_EXCL      0x0080u
#define O_TRUNC     0x0200u
#define O_APPEND    0x0400u
#define O_NONBLOCK  0x0800u
#define O_CLOEXEC   0x80000u

/* -------- open: path validation -------- */

static void t_open_no_create_enoent(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open("/tmp/nope", O_RDONLY, 0);
    TEST("open /tmp/nope without O_CREAT -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_open_creates_file(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/foo", O_CREAT | O_RDWR, 0644);
    TEST("open /tmp/foo with O_CREAT -> fd in tmpfs range");
    if (fd >= LINUX_TMPFS_FD_BASE &&
        fd <  LINUX_TMPFS_FD_BASE + LINUX_TMPFS_MAX_HANDLES) PASS();
    else FAIL("fd out of range");
}

static void t_open_excl_eexist(void) {
    linux_tmpfs_reset_for_tests();
    linux_tmpfs_open("/tmp/foo", O_CREAT, 0);
    int64_t r = linux_tmpfs_open("/tmp/foo", O_CREAT | O_EXCL, 0);
    TEST("open with O_CREAT|O_EXCL on existing file -> -EEXIST");
    if (r == -LINUX_EEXIST) PASS();
    else FAIL("EEXIST not surfaced");
}

static void t_open_excl_without_creat(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open("/tmp/foo", O_EXCL, 0);
    TEST("open with O_EXCL but no O_CREAT -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_open_unknown_flag(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open("/tmp/foo",
                                 O_CREAT | 0x10000000u, 0);
    TEST("open with unknown flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_open_bad_accmode(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open("/tmp/foo", O_CREAT | 0x3u, 0);
    TEST("open with O_ACCMODE all-bits pattern -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_open_null_path(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open(NULL, O_CREAT, 0);
    TEST("open NULL path -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_open_outside_tmp_enoent(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r1 = linux_tmpfs_open("/etc/foo", O_CREAT, 0);
    int64_t r2 = linux_tmpfs_open("/tmp", O_CREAT, 0);
    int64_t r3 = linux_tmpfs_open("/tmp/", O_CREAT, 0);
    TEST("open outside /tmp/ namespace -> -ENOENT");
    if (r1 == -LINUX_ENOENT && r2 == -LINUX_ENOENT && r3 == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_open_subdir_enoent(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_open("/tmp/sub/foo", O_CREAT, 0);
    TEST("open /tmp/<sub>/<name> (subdirs not supported) -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("subdirs accepted");
}

static void t_open_long_name(void) {
    linux_tmpfs_reset_for_tests();
    char path[200];
    /* 5 (prefix) + 130 = 135 chars total; exceed MAX_NAME=128. */
    for (size_t i = 0; i < 5; i++) path[i] = "/tmp/"[i];
    for (size_t i = 5; i < 135; i++) path[i] = 'a';
    path[135] = '\0';
    int64_t r = linux_tmpfs_open(path, O_CREAT, 0);
    TEST("open name >= LINUX_TMPFS_MAX_NAME -> -ENAMETOOLONG");
    if (r == -LINUX_ENAMETOOLONG) PASS();
    else FAIL("ENAMETOOLONG not surfaced");
}

static void t_open_table_full(void) {
    linux_tmpfs_reset_for_tests();
    char p[16];
    for (int i = 0; i < LINUX_TMPFS_MAX_FILES; i++) {
        p[0] = '/'; p[1] = 't'; p[2] = 'm'; p[3] = 'p';
        p[4] = '/'; p[5] = (char)('a' + i); p[6] = '\0';
        if (linux_tmpfs_open(p, O_CREAT, 0) < 0) {
            TEST("pre-fill"); FAIL("alloc failed early"); return;
        }
    }
    int64_t r = linux_tmpfs_open("/tmp/extra", O_CREAT, 0);
    TEST("open beyond MAX_FILES -> -ENOSPC");
    if (r == -LINUX_ENOSPC) PASS();
    else FAIL("ENOSPC not surfaced");
}

static void t_open_existing_returns_new_handle(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd1 = linux_tmpfs_open("/tmp/x", O_CREAT, 0);
    int64_t fd2 = linux_tmpfs_open("/tmp/x", O_RDWR, 0);
    TEST("open same name twice returns distinct handles");
    if (fd1 != fd2 && fd1 >= LINUX_TMPFS_FD_BASE && fd2 >= LINUX_TMPFS_FD_BASE)
        PASS();
    else FAIL("handles not distinct");
}

/* -------- read/write -------- */

static void t_write_then_read(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/hello", O_CREAT | O_RDWR, 0);
    int64_t w = linux_tmpfs_write_fd((int)fd, "hello", 5);
    /* Rewind then read. */
    linux_tmpfs_lseek_fd((int)fd, 0, 0);
    char buf[8] = {0};
    int64_t r = linux_tmpfs_read_fd((int)fd, buf, 8);
    TEST("write 'hello' then read after rewind returns same bytes");
    if (w == 5 && r == 5 && memcmp(buf, "hello", 5) == 0) PASS();
    else FAIL("roundtrip wrong");
}

static void t_read_eof(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/empty", O_CREAT | O_RDWR, 0);
    char buf[4];
    int64_t r = linux_tmpfs_read_fd((int)fd, buf, 4);
    TEST("read on empty file returns 0 (EOF)");
    if (r == 0) PASS();
    else FAIL("EOF not surfaced");
}

static void t_write_grows_size(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/grow", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd, "abc", 3);
    /* Lseek to END returns current size; should be 3. */
    int64_t end = linux_tmpfs_lseek_fd((int)fd, 0, 2);
    TEST("write extends file size (lseek SEEK_END returns 3)");
    if (end == 3) PASS();
    else FAIL("size not updated");
}

static void t_write_short_when_full(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/big", O_CREAT | O_RDWR, 0);
    /* Position at MAX_FILE_SIZE - 2; write 4 bytes -> should write 2. */
    linux_tmpfs_lseek_fd((int)fd, LINUX_TMPFS_MAX_FILE_SIZE - 2, 0);
    int64_t w = linux_tmpfs_write_fd((int)fd, "abcd", 4);
    TEST("write near MAX_FILE_SIZE returns short count (2 bytes)");
    if (w == 2) PASS();
    else FAIL("short write wrong");
}

static void t_write_full_enospc(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/full", O_CREAT | O_RDWR, 0);
    linux_tmpfs_lseek_fd((int)fd, LINUX_TMPFS_MAX_FILE_SIZE, 0);
    int64_t r = linux_tmpfs_write_fd((int)fd, "x", 1);
    TEST("write at exact MAX_FILE_SIZE returns -ENOSPC");
    if (r == -LINUX_ENOSPC) PASS();
    else FAIL("ENOSPC not surfaced");
}

static void t_write_zero_len(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/zero", O_CREAT | O_RDWR, 0);
    int64_t r = linux_tmpfs_write_fd((int)fd, NULL, 0);
    TEST("write len=0 returns 0 (NULL buf accepted)");
    if (r == 0) PASS();
    else FAIL("zero-len write failed");
}

static void t_read_null_buf(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/x", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd, "abc", 3);
    linux_tmpfs_lseek_fd((int)fd, 0, 0);
    int64_t r = linux_tmpfs_read_fd((int)fd, NULL, 8);
    TEST("read NULL buf with len > 0 -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_read_rdonly_then_write(void) {
    /* O_RDONLY: writes must fail with -EBADF. */
    linux_tmpfs_reset_for_tests();
    linux_tmpfs_open("/tmp/ro", O_CREAT, 0);  /* create */
    int64_t fd = linux_tmpfs_open("/tmp/ro", O_RDONLY, 0);
    int64_t r = linux_tmpfs_write_fd((int)fd, "x", 1);
    TEST("write to O_RDONLY handle -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_write_wronly_then_read(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/wo", O_CREAT | O_WRONLY, 0);
    char buf[4];
    int64_t r = linux_tmpfs_read_fd((int)fd, buf, 4);
    TEST("read on O_WRONLY handle -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* -------- O_TRUNC, O_APPEND -------- */

static void t_open_trunc_resets_size(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd1 = linux_tmpfs_open("/tmp/t", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd1, "abcdef", 6);
    linux_tmpfs_close((int)fd1);
    /* Reopen with O_TRUNC -> size should be 0. */
    int64_t fd2 = linux_tmpfs_open("/tmp/t", O_RDWR | O_TRUNC, 0);
    int64_t end = linux_tmpfs_lseek_fd((int)fd2, 0, 2);
    TEST("open with O_TRUNC resets size to 0");
    if (end == 0) PASS();
    else FAIL("size not reset");
}

static void t_open_append_seeks_to_end(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd1 = linux_tmpfs_open("/tmp/a", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd1, "abc", 3);
    linux_tmpfs_close((int)fd1);
    /* Reopen with O_APPEND -- the cursor should be at 3 already. */
    int64_t fd2 = linux_tmpfs_open("/tmp/a", O_RDWR | O_APPEND, 0);
    /* Tell -- SEEK_CUR with offset 0. */
    int64_t pos = linux_tmpfs_lseek_fd((int)fd2, 0, 1);
    TEST("open with O_APPEND positions cursor at end (3)");
    if (pos == 3) PASS();
    else FAIL("cursor not at end");
}

static void t_append_writes_always_end(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/a", O_CREAT | O_RDWR | O_APPEND, 0);
    linux_tmpfs_write_fd((int)fd, "abc", 3);
    /* Seek back to 0 -- but next write should still go to end. */
    linux_tmpfs_lseek_fd((int)fd, 0, 0);
    linux_tmpfs_write_fd((int)fd, "XYZ", 3);
    /* File should be "abcXYZ". */
    linux_tmpfs_lseek_fd((int)fd, 0, 0);
    char buf[8] = {0};
    int64_t r = linux_tmpfs_read_fd((int)fd, buf, 6);
    TEST("O_APPEND writes always go to end (file == 'abcXYZ')");
    if (r == 6 && memcmp(buf, "abcXYZ", 6) == 0) PASS();
    else FAIL("append semantics wrong");
}

/* -------- lseek -------- */

static void t_lseek_set_cur_end(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/s", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd, "hello", 5);

    int64_t r1 = linux_tmpfs_lseek_fd((int)fd, 1, 0);  /* SEEK_SET */
    int64_t r2 = linux_tmpfs_lseek_fd((int)fd, 2, 1);  /* SEEK_CUR */
    int64_t r3 = linux_tmpfs_lseek_fd((int)fd, -1, 2); /* SEEK_END */
    TEST("lseek SEEK_SET 1, then SEEK_CUR 2 (=3), then SEEK_END -1 (=4)");
    if (r1 == 1 && r2 == 3 && r3 == 4) PASS();
    else FAIL("lseek semantics wrong");
}

static void t_lseek_negative_einval(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/s", O_CREAT, 0);
    int64_t r = linux_tmpfs_lseek_fd((int)fd, -1, 0);
    TEST("lseek to negative offset -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_unknown_whence(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/s", O_CREAT, 0);
    int64_t r = linux_tmpfs_lseek_fd((int)fd, 0, 99);
    TEST("lseek unknown whence -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_past_eof(void) {
    /* Linux allows seeking past EOF; tmpfs follows. */
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/sparse", O_CREAT | O_RDWR, 0);
    int64_t r = linux_tmpfs_lseek_fd((int)fd, 100, 0);
    TEST("lseek past EOF returns the requested offset (sparse-friendly)");
    if (r == 100) PASS();
    else FAIL("clamp incorrect");
}

/* -------- close + refcount -------- */

static void t_close_releases_handle(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd1 = linux_tmpfs_open("/tmp/c", O_CREAT | O_RDWR, 0);
    int64_t r = linux_tmpfs_close((int)fd1);
    int64_t fd2 = linux_tmpfs_open("/tmp/c", O_RDWR, 0);
    TEST("close releases handle slot for re-use");
    if (r == 0 && fd2 == fd1) PASS();
    else FAIL("slot not reused");
}

static void t_close_bad_fd(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_close(0xDEAD);
    TEST("close fd outside tmpfs range -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* -------- unlink semantics -------- */

static void t_unlink_basic(void) {
    linux_tmpfs_reset_for_tests();
    linux_tmpfs_open("/tmp/u", O_CREAT, 0);
    int64_t r = linux_tmpfs_unlink("/tmp/u");
    int64_t fd = linux_tmpfs_open("/tmp/u", O_RDWR, 0);
    TEST("unlink removes name; subsequent open without O_CREAT -> -ENOENT");
    if (r == 0 && fd == -LINUX_ENOENT) PASS();
    else FAIL("unlink semantics wrong");
}

static void t_unlink_missing(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_unlink("/tmp/missing");
    TEST("unlink missing path -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_unlink_keeps_existing_handle(void) {
    /* POSIX: unlink while a handle is open keeps the file alive. */
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/orphan", O_CREAT | O_RDWR, 0);
    linux_tmpfs_write_fd((int)fd, "abc", 3);
    linux_tmpfs_unlink("/tmp/orphan");
    /* The fd should still work. */
    linux_tmpfs_lseek_fd((int)fd, 0, 0);
    char buf[4] = {0};
    int64_t r = linux_tmpfs_read_fd((int)fd, buf, 3);
    TEST("unlink keeps existing handle alive (read still works)");
    if (r == 3 && memcmp(buf, "abc", 3) == 0) PASS();
    else FAIL("orphan handle broken");
}

static void t_unlink_then_close_reaps(void) {
    linux_tmpfs_reset_for_tests();
    int64_t fd = linux_tmpfs_open("/tmp/reap", O_CREAT, 0);
    linux_tmpfs_unlink("/tmp/reap");
    linux_tmpfs_close((int)fd);
    /* Slot should be free again -- next open with same name and
     * O_CREAT must succeed and end up at a usable slot. */
    int64_t fd2 = linux_tmpfs_open("/tmp/reap", O_CREAT, 0);
    TEST("unlink + close reaps slot (re-create on same name works)");
    if (fd2 >= LINUX_TMPFS_FD_BASE) PASS();
    else FAIL("slot not reaped");
}

static void t_unlink_outside_tmp(void) {
    linux_tmpfs_reset_for_tests();
    int64_t r = linux_tmpfs_unlink("/etc/foo");
    TEST("unlink outside /tmp/ namespace -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

int test_linux_tmpfs_run(void) {
    printf("[test_linux_tmpfs]\n");
    tests_run = tests_passed = 0;

    t_open_no_create_enoent();
    t_open_creates_file();
    t_open_excl_eexist();
    t_open_excl_without_creat();
    t_open_unknown_flag();
    t_open_bad_accmode();
    t_open_null_path();
    t_open_outside_tmp_enoent();
    t_open_subdir_enoent();
    t_open_long_name();
    t_open_table_full();
    t_open_existing_returns_new_handle();

    t_write_then_read();
    t_read_eof();
    t_write_grows_size();
    t_write_short_when_full();
    t_write_full_enospc();
    t_write_zero_len();
    t_read_null_buf();
    t_read_rdonly_then_write();
    t_write_wronly_then_read();

    t_open_trunc_resets_size();
    t_open_append_seeks_to_end();
    t_append_writes_always_end();

    t_lseek_set_cur_end();
    t_lseek_negative_einval();
    t_lseek_unknown_whence();
    t_lseek_past_eof();

    t_close_releases_handle();
    t_close_bad_fd();

    t_unlink_basic();
    t_unlink_missing();
    t_unlink_keeps_existing_handle();
    t_unlink_then_close_reaps();
    t_unlink_outside_tmp();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
