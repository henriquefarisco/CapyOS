/* Host tests for linux_inotify (S1.16). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_inotify.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* -------- init1 -------- */

static void t_init1_basic(void) {
    linux_inotify_reset_for_tests();
    int64_t r = linux_inotify_init1(0);
    TEST("inotify_init1: basic returns fd in inotify range");
    if (r >= LINUX_INOTIFY_FD_BASE &&
        r < LINUX_INOTIFY_FD_BASE + LINUX_INOTIFY_MAX_INSTANCES) PASS();
    else FAIL("fd out of range");
}

static void t_init1_unknown_flag(void) {
    linux_inotify_reset_for_tests();
    int64_t r = linux_inotify_init1(0x1000u);
    TEST("inotify_init1: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_init1_known_flags(void) {
    linux_inotify_reset_for_tests();
    int64_t r1 = linux_inotify_init1(LINUX_IN_CLOEXEC);
    int64_t r2 = linux_inotify_init1(LINUX_IN_NONBLOCK);
    int64_t r3 = linux_inotify_init1(LINUX_IN_CLOEXEC | LINUX_IN_NONBLOCK);
    TEST("inotify_init1: IN_CLOEXEC/IN_NONBLOCK and combo accepted");
    if (r1 >= 0 && r2 >= 0 && r3 >= 0) PASS();
    else FAIL("known flags rejected");
}

static void t_init1_table_full(void) {
    linux_inotify_reset_for_tests();
    for (int i = 0; i < LINUX_INOTIFY_MAX_INSTANCES; i++) {
        if (linux_inotify_init1(0) < 0) { FAIL("alloc failed early"); return; }
    }
    int64_t r = linux_inotify_init1(0);
    TEST("inotify_init1: table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

/* -------- add_watch -------- */

static void t_add_watch_basic(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    const char *path = "/etc/foo";
    int64_t r = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)path,
                                        LINUX_IN_MODIFY);
    TEST("inotify_add_watch: returns wd >= 1");
    if (r >= 1) PASS();
    else FAIL("wd not allocated");
}

static void t_add_watch_unique_wd(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t a = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a",
                                        LINUX_IN_MODIFY);
    int64_t b = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/b",
                                        LINUX_IN_MODIFY);
    TEST("inotify_add_watch: distinct watches get distinct wds");
    if (a > 0 && b > 0 && a != b) PASS();
    else FAIL("wd collision");
}

static void t_add_watch_null_path(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_inotify_add_watch(fd, 0, LINUX_IN_MODIFY);
    TEST("inotify_add_watch: NULL path -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_add_watch_zero_mask(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a", 0);
    TEST("inotify_add_watch: empty mask -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("empty mask accepted");
}

static void t_add_watch_unknown_mask(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    /* Bit outside known mask. */
    int64_t r = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a",
                                        0x00800000u);
    TEST("inotify_add_watch: unknown mask bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown mask accepted");
}

static void t_add_watch_no_event_bit(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    /* Only flag bits, no event bits. */
    int64_t r = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a",
                                        LINUX_IN_ONLYDIR);
    TEST("inotify_add_watch: mask without any event bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("event-less mask accepted");
}

static void t_add_watch_bad_fd(void) {
    linux_inotify_reset_for_tests();
    int64_t r = linux_inotify_add_watch(99, (uint64_t)(uintptr_t)"/a",
                                        LINUX_IN_MODIFY);
    TEST("inotify_add_watch: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_add_watch_table_full(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    for (int i = 0; i < LINUX_INOTIFY_MAX_PER_INSTANCE; i++) {
        if (linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a",
                                    LINUX_IN_MODIFY) < 0) {
            FAIL("alloc failed early"); return;
        }
    }
    int64_t r = linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/b",
                                        LINUX_IN_MODIFY);
    TEST("inotify_add_watch: instance watch list full -> -ENOMEM");
    if (r == -LINUX_ENOMEM) PASS();
    else FAIL("ENOMEM not surfaced");
}

/* -------- rm_watch -------- */

static void t_rm_watch_basic(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int wd = (int)linux_inotify_add_watch(fd, (uint64_t)(uintptr_t)"/a",
                                          LINUX_IN_MODIFY);
    int64_t r = linux_inotify_rm_watch(fd, wd);
    TEST("inotify_rm_watch: removes existing wd");
    if (r == 0) PASS();
    else FAIL("rm failed");
}

static void t_rm_watch_unknown_wd(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_inotify_rm_watch(fd, 12345);
    TEST("inotify_rm_watch: unknown wd -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown wd accepted");
}

static void t_rm_watch_bad_fd(void) {
    linux_inotify_reset_for_tests();
    int64_t r = linux_inotify_rm_watch(99, 1);
    TEST("inotify_rm_watch: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* -------- fd lifecycle / generic I/O -------- */

static void t_close_releases_slot(void) {
    linux_inotify_reset_for_tests();
    int64_t fd = linux_inotify_init1(0);
    int64_t c = linux_inotify_close((int)fd);
    int64_t again = linux_inotify_init1(0);
    TEST("inotify close releases fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("slot not reused");
}

static void t_read_empty_eagain(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    uint8_t buf[16];
    int64_t r = linux_inotify_read(fd, buf, sizeof(buf));
    TEST("inotify read: no generated events yet -> -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("EAGAIN not surfaced");
}

static void t_write_einval(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_inotify_write(fd, "x", 1);
    TEST("inotify write: not supported -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_espipe(void) {
    linux_inotify_reset_for_tests();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_inotify_lseek(fd, 0, 0);
    TEST("inotify lseek: stream fd -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("ESPIPE not surfaced");
}

static void t_read_bad_fd(void) {
    linux_inotify_reset_for_tests();
    uint8_t buf[16];
    int64_t r = linux_inotify_read(LINUX_INOTIFY_FD_BASE, buf, sizeof(buf));
    TEST("inotify read: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

int test_linux_inotify_run(void) {
    printf("[test_linux_inotify]\n");
    tests_run = tests_passed = 0;

    t_init1_basic();
    t_init1_unknown_flag();
    t_init1_known_flags();
    t_init1_table_full();

    t_add_watch_basic();
    t_add_watch_unique_wd();
    t_add_watch_null_path();
    t_add_watch_zero_mask();
    t_add_watch_unknown_mask();
    t_add_watch_no_event_bit();
    t_add_watch_bad_fd();
    t_add_watch_table_full();

    t_rm_watch_basic();
    t_rm_watch_unknown_wd();
    t_rm_watch_bad_fd();

    t_close_releases_slot();
    t_read_empty_eagain();
    t_write_einval();
    t_lseek_espipe();
    t_read_bad_fd();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
