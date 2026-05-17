#include "kernel/linux_compat/linux_statfs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_blocks_calls;
static int g_files_calls;
static uint64_t fake_blocks(void) { g_blocks_calls++; return 32768; }
static uint64_t fake_files(void) { g_files_calls++; return 4096; }

static void install_fake(void) {
    static const struct linux_statfs_providers p = {
        .total_blocks = fake_blocks,
        .total_files  = fake_files,
    };
    g_blocks_calls = g_files_calls = 0;
    linux_statfs_reset_for_tests();
    linux_statfs_install_providers(&p);
}

static void t1(void) {
    linux_statfs_reset_for_tests();
    TEST("statfs(NULL, &buf) -> -EFAULT");
    struct linux_statfs s;
    if (linux_statfs(NULL, &s) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_statfs_reset_for_tests();
    TEST("statfs(\"\", &buf) -> -ENOENT");
    struct linux_statfs s;
    if (linux_statfs("", &s) == -LINUX_ENOENT) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_statfs_reset_for_tests();
    TEST("statfs(\"/x\", NULL) -> -EFAULT");
    if (linux_statfs("/x", NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_statfs_reset_for_tests();
    struct linux_statfs s;
    int64_t r = linux_statfs("/x", &s);
    TEST("statfs without providers -> default 16384 blocks");
    if (r == 0 && s.f_type == (int64_t)LINUX_STATFS_TMPFS_MAGIC &&
        s.f_bsize == 4096 && s.f_blocks == 16384 &&
        s.f_bfree == 16384 && s.f_bavail == 16384 &&
        s.f_files == 1024 && s.f_ffree == 1024 &&
        s.f_namelen == 255 && s.f_frsize == 4096 &&
        s.f_flags == 0) PASS();
    else FAIL("");
}
static void t5(void) {
    install_fake();
    struct linux_statfs s;
    int64_t r = linux_statfs("/x", &s);
    TEST("statfs with providers uses injected counts");
    if (r == 0 && s.f_blocks == 32768 && s.f_files == 4096) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_statfs_reset_for_tests();
    TEST("fstatfs(-1, &buf) -> -EBADF");
    struct linux_statfs s;
    if (linux_fstatfs(-1, &s) == -LINUX_EBADF) PASS();
    else FAIL("");
}
static void t7(void) {
    linux_statfs_reset_for_tests();
    TEST("fstatfs(7, NULL) -> -EFAULT");
    if (linux_fstatfs(7, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t8(void) {
    install_fake();
    struct linux_statfs s;
    int64_t r = linux_fstatfs(7, &s);
    TEST("fstatfs(7) -> ok with provider counts");
    if (r == 0 && s.f_blocks == 32768) PASS();
    else FAIL("");
}
static void t9(void) {
    install_fake();
    struct linux_statfs s;
    /* Make sure spare and fsid are zeroed. */
    int64_t r = linux_statfs("/x", &s);
    TEST("statfs zeroes f_fsid and f_spare");
    int ok = (r == 0);
    for (int i = 0; i < 2; i++) if (s.f_fsid[i] != 0) ok = 0;
    for (int i = 0; i < 4; i++) if (s.f_spare[i] != 0) ok = 0;
    if (ok) PASS();
    else FAIL("");
}
static void t10(void) {
    install_fake();
    linux_statfs_install_providers(NULL);
    struct linux_statfs s;
    int64_t r = linux_statfs("/x", &s);
    TEST("statfs install_providers(NULL) clears providers");
    if (r == 0 && s.f_blocks == 16384 && s.f_files == 1024 &&
        g_blocks_calls == 0 && g_files_calls == 0) PASS();
    else FAIL("");
}
static void t11(void) {
    install_fake();
    linux_statfs_reset_for_tests();
    struct linux_statfs s;
    int64_t r = linux_fstatfs(7, &s);
    TEST("statfs reset clears installed providers");
    if (r == 0 && s.f_blocks == 16384 && s.f_files == 1024 &&
        g_blocks_calls == 0 && g_files_calls == 0) PASS();
    else FAIL("");
}

int test_linux_statfs_run(void) {
    printf("[test_linux_statfs]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10(); t11();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
