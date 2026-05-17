/*
 * Host tests for the Linux-ABI pseudo-/dev shim
 * (`include/kernel/linux_compat/linux_devfs.h` /
 *  `src/kernel/linux_compat/linux_devfs.c`).
 *
 * /dev/{null,zero,full,urandom,random} contracts under the canonical
 * Linux 6.x semantics:
 *   /dev/null     read EOF / write sink
 *   /dev/zero     read 0x00 / write sink
 *   /dev/full     read 0x00 / write -ENOSPC (mirror Linux)
 *   /dev/urandom  read CSPRNG / write sink
 *   /dev/random   alias of urandom
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_devfs.h"
#include "kernel/linux_compat/linux_random.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-72s ", name);                                          \
    } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* Deterministic random source for urandom assertions. */
static uint8_t g_counter_next;
static void counter_source(void *buf, size_t len) {
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) out[i] = g_counter_next++;
}

static void install_counter_source(void) {
    linux_random_reset_for_tests();
    linux_devfs_reset_for_tests();
    g_counter_next = 0;
    linux_random_install_source(counter_source);
}

/* --------------------------------------------------------------------- */

static void test_lookup_known_paths(void) {
    install_counter_source();

    enum linux_devfs_id n  = linux_devfs_lookup("/dev/null");
    enum linux_devfs_id z  = linux_devfs_lookup("/dev/zero");
    enum linux_devfs_id f  = linux_devfs_lookup("/dev/full");
    enum linux_devfs_id u  = linux_devfs_lookup("/dev/urandom");
    enum linux_devfs_id r  = linux_devfs_lookup("/dev/random");

    TEST("lookup: /dev/{null,zero,full,urandom,random} all resolve");
    if (n == LINUX_DEV_NULL && z == LINUX_DEV_ZERO && f == LINUX_DEV_FULL &&
        u == LINUX_DEV_URANDOM && r == LINUX_DEV_RANDOM) PASS();
    else FAIL("lookup table incomplete");
}

static void test_lookup_unknown_paths(void) {
    install_counter_source();

    enum linux_devfs_id a = linux_devfs_lookup("/dev/sda");
    enum linux_devfs_id b = linux_devfs_lookup("/dev/");
    enum linux_devfs_id c = linux_devfs_lookup("/etc/passwd");
    enum linux_devfs_id d = linux_devfs_lookup("");
    enum linux_devfs_id e = linux_devfs_lookup(NULL);

    TEST("lookup: unknown paths return LINUX_DEV_NONE");
    if (a == LINUX_DEV_NONE && b == LINUX_DEV_NONE && c == LINUX_DEV_NONE &&
        d == LINUX_DEV_NONE && e == LINUX_DEV_NONE) PASS();
    else FAIL("unknown path matched");
}

static void test_lookup_case_sensitive(void) {
    install_counter_source();

    /* Linux paths are case-sensitive. */
    enum linux_devfs_id a = linux_devfs_lookup("/DEV/NULL");
    enum linux_devfs_id b = linux_devfs_lookup("/dev/NULL");

    TEST("lookup: paths are case-sensitive (Linux semantics)");
    if (a == LINUX_DEV_NONE && b == LINUX_DEV_NONE) PASS();
    else FAIL("case insensitive match leaked");
}

static void test_read_null_eof(void) {
    install_counter_source();
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 0xCC;
    int64_t r = linux_devfs_read(LINUX_DEV_NULL, buf, sizeof(buf));

    TEST("read /dev/null: returns 0 (EOF) and does not touch buf");
    int untouched = 1;
    for (int i = 0; i < 16; i++) {
        if (buf[i] != 0xCC) { untouched = 0; break; }
    }
    if (r == 0 && untouched) PASS();
    else FAIL("read corrupted buf or returned non-zero");
}

static void test_read_zero_fills_zeros(void) {
    install_counter_source();
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0xFF;
    int64_t r = linux_devfs_read(LINUX_DEV_ZERO, buf, sizeof(buf));

    int all_zero = 1;
    for (int i = 0; i < 8; i++) {
        if (buf[i] != 0x00) { all_zero = 0; break; }
    }
    TEST("read /dev/zero: returns len and fills buf with 0x00");
    if (r == 8 && all_zero) PASS();
    else FAIL("buf not all-zero or wrong return");
}

static void test_read_full_fills_zeros(void) {
    install_counter_source();
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0xFF;
    int64_t r = linux_devfs_read(LINUX_DEV_FULL, buf, sizeof(buf));

    int all_zero = 1;
    for (int i = 0; i < 8; i++) {
        if (buf[i] != 0x00) { all_zero = 0; break; }
    }
    TEST("read /dev/full: returns len and fills buf with 0x00");
    if (r == 8 && all_zero) PASS();
    else FAIL("buf not all-zero or wrong return");
}

static void test_read_urandom_uses_csprng(void) {
    install_counter_source();
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0;
    int64_t r = linux_devfs_read(LINUX_DEV_URANDOM, buf, sizeof(buf));

    /* Counter source emits ascending bytes from 0 (it is fresh). */
    int ascending = 1;
    for (int i = 0; i < 8; i++) {
        if (buf[i] != (uint8_t)i) { ascending = 0; break; }
    }
    TEST("read /dev/urandom: bytes come from CSPRNG source");
    if (r == 8 && ascending) PASS();
    else FAIL("urandom did not delegate to source");
}

static void test_read_random_aliases_urandom(void) {
    install_counter_source();
    uint8_t buf_a[4];
    uint8_t buf_b[4];
    int64_t ra = linux_devfs_read(LINUX_DEV_URANDOM, buf_a, sizeof(buf_a));
    int64_t rb = linux_devfs_read(LINUX_DEV_RANDOM,  buf_b, sizeof(buf_b));

    /* Both should read 4 bytes and consume from the same pool: byte
     * stream 0..7 split. */
    TEST("read /dev/random: aliases /dev/urandom (same CSPRNG)");
    if (ra == 4 && rb == 4 &&
        buf_a[0] == 0 && buf_a[3] == 3 &&
        buf_b[0] == 4 && buf_b[3] == 7) PASS();
    else FAIL("random did not share urandom pool");
}

static void test_read_zero_len(void) {
    install_counter_source();
    uint8_t buf[4] = {0xCC};
    int64_t r1 = linux_devfs_read(LINUX_DEV_ZERO,    buf, 0);
    int64_t r2 = linux_devfs_read(LINUX_DEV_URANDOM, buf, 0);
    int64_t r3 = linux_devfs_read(LINUX_DEV_NULL,    buf, 0);

    TEST("read: len=0 returns 0 for any device, never touches buf");
    if (r1 == 0 && r2 == 0 && r3 == 0 && buf[0] == 0xCC) PASS();
    else FAIL("len=0 corrupted buf or returned non-zero");
}

static void test_read_null_buf(void) {
    install_counter_source();
    int64_t r1 = linux_devfs_read(LINUX_DEV_ZERO, NULL, 16);
    int64_t r2 = linux_devfs_read(LINUX_DEV_URANDOM, NULL, 16);

    TEST("read: NULL buf with len > 0 returns -EFAULT");
    if (r1 == -LINUX_EFAULT && r2 == -LINUX_EFAULT) PASS();
    else FAIL("NULL buf not rejected");
}

static void test_read_unknown_device(void) {
    install_counter_source();
    uint8_t buf[4];
    int64_t r = linux_devfs_read(LINUX_DEV_NONE, buf, sizeof(buf));

    TEST("read LINUX_DEV_NONE: returns -ENODEV");
    if (r == -LINUX_ENODEV) PASS();
    else FAIL("did not return -ENODEV");
}

static void test_write_null_sinks(void) {
    install_counter_source();
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    int64_t r1 = linux_devfs_write(LINUX_DEV_NULL,    buf, sizeof(buf));
    int64_t r2 = linux_devfs_write(LINUX_DEV_ZERO,    buf, sizeof(buf));
    int64_t r3 = linux_devfs_write(LINUX_DEV_URANDOM, buf, sizeof(buf));
    int64_t r4 = linux_devfs_write(LINUX_DEV_RANDOM,  buf, sizeof(buf));

    TEST("write: /dev/{null,zero,urandom,random} are sinks (return len)");
    if (r1 == 8 && r2 == 8 && r3 == 8 && r4 == 8) PASS();
    else FAIL("at least one sink misbehaved");
}

static void test_write_full_returns_enospc(void) {
    install_counter_source();
    uint8_t buf[8] = {1};
    int64_t r = linux_devfs_write(LINUX_DEV_FULL, buf, sizeof(buf));

    TEST("write /dev/full: returns -ENOSPC (Linux semantics)");
    if (r == -LINUX_ENOSPC) PASS();
    else FAIL("did not return -ENOSPC");
}

static void test_write_zero_len(void) {
    install_counter_source();
    int64_t r1 = linux_devfs_write(LINUX_DEV_ZERO, "x", 0);
    int64_t r2 = linux_devfs_write(LINUX_DEV_FULL, "x", 0);
    int64_t r3 = linux_devfs_write(LINUX_DEV_NULL, NULL, 0);

    TEST("write: len=0 returns 0 for any device (incl. /dev/full)");
    if (r1 == 0 && r2 == 0 && r3 == 0) PASS();
    else FAIL("len=0 path inconsistent");
}

static void test_write_null_buf(void) {
    install_counter_source();
    int64_t r1 = linux_devfs_write(LINUX_DEV_NULL, NULL, 8);
    int64_t r2 = linux_devfs_write(LINUX_DEV_FULL, NULL, 8);

    TEST("write: NULL buf with len > 0 returns -EFAULT");
    if (r1 == -LINUX_EFAULT && r2 == -LINUX_EFAULT) PASS();
    else FAIL("NULL buf not rejected on write path");
}

static void test_write_unknown_device(void) {
    install_counter_source();
    uint8_t buf[4] = {1};
    int64_t r = linux_devfs_write(LINUX_DEV_NONE, buf, sizeof(buf));

    TEST("write LINUX_DEV_NONE: returns -ENODEV");
    if (r == -LINUX_ENODEV) PASS();
    else FAIL("did not return -ENODEV");
}

static void test_urandom_eagain_when_source_uninstalled(void) {
    /* If random source is reset, /dev/urandom propagates -EAGAIN
     * (the linux_getrandom contract). */
    linux_random_reset_for_tests();
    linux_devfs_reset_for_tests();
    uint8_t buf[4];
    int64_t r = linux_devfs_read(LINUX_DEV_URANDOM, buf, sizeof(buf));

    TEST("read /dev/urandom: -EAGAIN when CSPRNG source not installed");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("did not propagate -EAGAIN");
}

/* -------- fd-based API used by the linux_vfs router -------- */

static void test_fd_open_known(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/null", 0);
    TEST("fd: open /dev/null returns fd in devfs range");
    if (fd >= LINUX_DEVFS_FD_BASE &&
        fd <  LINUX_DEVFS_FD_BASE + LINUX_DEVFS_MAX_INSTANCES) PASS();
    else FAIL("fd out of range");
}

static void test_fd_open_unknown_enoent(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/unicorn", 0);
    TEST("fd: open unknown device -> -ENOENT");
    if (fd == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void test_fd_open_null_path_efault(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open(NULL, 0);
    TEST("fd: open NULL path -> -EFAULT");
    if (fd == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void test_fd_open_unknown_flag_einval(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/null", 0x10000000u);
    TEST("fd: open with unknown flag bit -> -EINVAL");
    if (fd == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void test_fd_open_bad_accmode(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/null", 0x3u);
    TEST("fd: open with O_ACCMODE all-bits pattern -> -EINVAL");
    if (fd == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void test_fd_open_table_full_emfile(void) {
    linux_devfs_reset_for_tests();
    for (int i = 0; i < LINUX_DEVFS_MAX_INSTANCES; i++) {
        int64_t fd = linux_devfs_open("/dev/zero", 0);
        if (fd < 0) { TEST("fd: pre-fill"); FAIL("alloc failed early"); return; }
    }
    int64_t fd = linux_devfs_open("/dev/zero", 0);
    TEST("fd: open when slot table is full -> -EMFILE");
    if (fd == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

static void test_fd_close_releases(void) {
    linux_devfs_reset_for_tests();
    int64_t fd1 = linux_devfs_open("/dev/zero", 0);
    int64_t cr = linux_devfs_close((int)fd1);
    int64_t fd2 = linux_devfs_open("/dev/zero", 0);
    TEST("fd: close releases slot for re-open (fd2 == fd1)");
    if (cr == 0 && fd2 == fd1) PASS();
    else FAIL("slot not reused");
}

static void test_fd_close_bad_fd_ebadf(void) {
    linux_devfs_reset_for_tests();
    int64_t r = linux_devfs_close(0xDEAD);
    TEST("fd: close fd outside devfs range -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void test_fd_close_unopened_ebadf(void) {
    linux_devfs_reset_for_tests();
    /* fd in range but not allocated. */
    int64_t r = linux_devfs_close(LINUX_DEVFS_FD_BASE + 5);
    TEST("fd: close unallocated slot -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void test_fd_read_dispatch(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/zero", 0);
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    int64_t r = linux_devfs_read_fd((int)fd, buf, 4);
    int all_zero = (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    TEST("fd: read_fd /dev/zero dispatches to id-based read");
    if (r == 4 && all_zero) PASS();
    else FAIL("dispatch wrong");
}

static void test_fd_write_dispatch_full(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/full", 0);
    uint8_t buf[4] = {1};
    int64_t r = linux_devfs_write_fd((int)fd, buf, 4);
    TEST("fd: write_fd /dev/full -> -ENOSPC via dispatch");
    if (r == -LINUX_ENOSPC) PASS();
    else FAIL("ENOSPC not surfaced via fd path");
}

static void test_fd_lseek_returns_zero(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/zero", 0);
    int64_t r1 = linux_devfs_lseek_fd((int)fd, 100, 0);   /* SET */
    int64_t r2 = linux_devfs_lseek_fd((int)fd, -50, 1);   /* CUR */
    int64_t r3 = linux_devfs_lseek_fd((int)fd, 0, 2);     /* END */
    TEST("fd: lseek_fd returns 0 for char devices (SET/CUR/END)");
    if (r1 == 0 && r2 == 0 && r3 == 0) PASS();
    else FAIL("non-zero pos");
}

static void test_fd_lseek_unknown_whence_einval(void) {
    linux_devfs_reset_for_tests();
    int64_t fd = linux_devfs_open("/dev/zero", 0);
    int64_t r = linux_devfs_lseek_fd((int)fd, 0, 99);
    TEST("fd: lseek_fd with unknown whence -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void test_fd_read_bad_fd_ebadf(void) {
    linux_devfs_reset_for_tests();
    uint8_t buf[4];
    int64_t r = linux_devfs_read_fd(0xDEAD, buf, 4);
    TEST("fd: read_fd with bad fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

int test_linux_devfs_run(void) {
    printf("[test_linux_devfs]\n");
    tests_run = 0;
    tests_passed = 0;

    test_lookup_known_paths();
    test_lookup_unknown_paths();
    test_lookup_case_sensitive();
    test_read_null_eof();
    test_read_zero_fills_zeros();
    test_read_full_fills_zeros();
    test_read_urandom_uses_csprng();
    test_read_random_aliases_urandom();
    test_read_zero_len();
    test_read_null_buf();
    test_read_unknown_device();
    test_write_null_sinks();
    test_write_full_returns_enospc();
    test_write_zero_len();
    test_write_null_buf();
    test_write_unknown_device();
    test_urandom_eagain_when_source_uninstalled();

    test_fd_open_known();
    test_fd_open_unknown_enoent();
    test_fd_open_null_path_efault();
    test_fd_open_unknown_flag_einval();
    test_fd_open_bad_accmode();
    test_fd_open_table_full_emfile();
    test_fd_close_releases();
    test_fd_close_bad_fd_ebadf();
    test_fd_close_unopened_ebadf();
    test_fd_read_dispatch();
    test_fd_write_dispatch_full();
    test_fd_lseek_returns_zero();
    test_fd_lseek_unknown_whence_einval();
    test_fd_read_bad_fd_ebadf();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
