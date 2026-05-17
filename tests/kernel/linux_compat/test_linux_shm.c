/* Host tests for linux_shm (S2.10). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_shm.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t_open_create(void) {
    linux_shm_reset_for_tests();
    int64_t fd = linux_shm_open("foo",
        LINUX_O_CREAT | LINUX_O_RDWR, 0600);
    TEST("shm_open O_CREAT: returns fd in shm range");
    if (fd >= LINUX_SHM_FD_BASE) PASS(); else FAIL("fd out of range");
}

static void t_open_existing(void) {
    linux_shm_reset_for_tests();
    int64_t a = linux_shm_open("foo", LINUX_O_CREAT, 0600);
    int64_t b = linux_shm_open("foo", LINUX_O_RDWR, 0);
    TEST("shm_open: open existing without O_CREAT works (returns same slot)");
    if (a >= 0 && b >= 0 && a == b) PASS();
    else FAIL("open existing failed");
}

static void t_open_excl_eexist(void) {
    linux_shm_reset_for_tests();
    linux_shm_open("foo", LINUX_O_CREAT, 0600);
    int64_t r = linux_shm_open("foo",
        LINUX_O_CREAT | LINUX_O_EXCL, 0600);
    TEST("shm_open O_CREAT|O_EXCL on existing -> -EEXIST");
    if (r == -LINUX_EEXIST) PASS(); else FAIL("EEXIST not surfaced");
}

static void t_open_no_create_enoent(void) {
    linux_shm_reset_for_tests();
    int64_t r = linux_shm_open("nonexistent", LINUX_O_RDWR, 0);
    TEST("shm_open without O_CREAT on absent -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS(); else FAIL("ENOENT not surfaced");
}

static void t_open_invalid_name(void) {
    linux_shm_reset_for_tests();
    int64_t r1 = linux_shm_open(NULL, LINUX_O_CREAT, 0);
    int64_t r2 = linux_shm_open("", LINUX_O_CREAT, 0);
    TEST("shm_open: NULL or empty name -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL) PASS();
    else FAIL("name validation wrong");
}

static void t_open_unknown_flag(void) {
    linux_shm_reset_for_tests();
    int64_t r = linux_shm_open("foo", 0x100000u, 0);
    TEST("shm_open: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("unknown flag accepted");
}

static void t_open_long_name(void) {
    linux_shm_reset_for_tests();
    char name[LINUX_SHM_MAX_NAME + 5];
    for (size_t i = 0; i < sizeof(name) - 1; i++) name[i] = 'a';
    name[sizeof(name) - 1] = '\0';
    int64_t r = linux_shm_open(name, LINUX_O_CREAT, 0);
    TEST("shm_open: name too long -> -ENAMETOOLONG");
    if (r == -LINUX_ENAMETOOLONG) PASS();
    else FAIL("long name accepted");
}

static void t_open_table_full(void) {
    linux_shm_reset_for_tests();
    char name[16];
    for (int i = 0; i < LINUX_SHM_MAX_OBJECTS; i++) {
        name[0] = (char)('a' + i); name[1] = '\0';
        if (linux_shm_open(name, LINUX_O_CREAT, 0) < 0) {
            FAIL("alloc failed early"); return;
        }
    }
    int64_t r = linux_shm_open("z", LINUX_O_CREAT, 0);
    TEST("shm_open: table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS(); else FAIL("EMFILE not surfaced");
}

static void t_truncate_basic(void) {
    linux_shm_reset_for_tests();
    int fd = (int)linux_shm_open("foo", LINUX_O_CREAT, 0);
    int64_t r = linux_shm_truncate(fd, 4096);
    int64_t s = linux_shm_size(fd);
    TEST("shm_truncate: stores size; shm_size returns it");
    if (r == 0 && s == 4096) PASS(); else FAIL("size mismatch");
}

static void t_truncate_too_large(void) {
    linux_shm_reset_for_tests();
    int fd = (int)linux_shm_open("foo", LINUX_O_CREAT, 0);
    int64_t r = linux_shm_truncate(fd, LINUX_SHM_MAX_SIZE + 1);
    TEST("shm_truncate: size > LINUX_SHM_MAX_SIZE -> -EFBIG");
    if (r == -LINUX_EFBIG) PASS(); else FAIL("EFBIG not surfaced");
}

static void t_truncate_bad_fd(void) {
    linux_shm_reset_for_tests();
    int64_t r = linux_shm_truncate(99, 4096);
    TEST("shm_truncate: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS(); else FAIL("EBADF not surfaced");
}

static void t_unlink_basic(void) {
    linux_shm_reset_for_tests();
    linux_shm_open("foo", LINUX_O_CREAT, 0);
    int64_t r = linux_shm_unlink("foo");
    TEST("shm_unlink: removes name from table");
    if (r == 0 && linux_shm_test_named_count() == 0) PASS();
    else FAIL("unlink left name");
}

static void t_unlink_missing_enoent(void) {
    linux_shm_reset_for_tests();
    int64_t r = linux_shm_unlink("nope");
    TEST("shm_unlink: missing name -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS(); else FAIL("ENOENT not surfaced");
}

static void t_close_decrements(void) {
    linux_shm_reset_for_tests();
    int fd = (int)linux_shm_open("foo", LINUX_O_CREAT, 0);
    int64_t r = linux_shm_close(fd);
    TEST("shm_close: returns 0 for valid fd");
    if (r == 0) PASS(); else FAIL("close failed");
}

static void t_unlink_then_close_frees_slot(void) {
    /* POSIX semantics: existing fds remain valid until close. */
    linux_shm_reset_for_tests();
    int fd = (int)linux_shm_open("foo", LINUX_O_CREAT, 0);
    linux_shm_unlink("foo");
    /* Slot must still be readable until close. */
    int64_t s_after_unlink = linux_shm_size(fd);
    linux_shm_close(fd);
    int64_t s_after_close = linux_shm_size(fd);
    TEST("shm: unlink+close orphan-frees slot (size becomes EBADF)");
    if (s_after_unlink == 0 && s_after_close == -LINUX_EBADF) PASS();
    else FAIL("orphan release wrong");
}

int test_linux_shm_run(void) {
    printf("[test_linux_shm]\n");
    tests_run = tests_passed = 0;

    t_open_create();
    t_open_existing();
    t_open_excl_eexist();
    t_open_no_create_enoent();
    t_open_invalid_name();
    t_open_unknown_flag();
    t_open_long_name();
    t_open_table_full();

    t_truncate_basic();
    t_truncate_too_large();
    t_truncate_bad_fd();

    t_unlink_basic();
    t_unlink_missing_enoent();

    t_close_decrements();
    t_unlink_then_close_frees_slot();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
