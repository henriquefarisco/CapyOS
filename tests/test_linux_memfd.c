/* Host tests for linux_memfd (S1.15 memfd_create + pidfd_*). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_memfd.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake pid_exists: pid 42 and 100 exist, others don't. */
static int g_pid_exists_calls;
static int fake_pid_exists(uint32_t pid) {
    g_pid_exists_calls++;
    return (pid == 42 || pid == 100) ? 1 : 0;
}

static void install_fake(void) {
    linux_memfd_reset_for_tests();
    g_pid_exists_calls = 0;
    static const struct linux_memfd_ops ops = { .pid_exists = fake_pid_exists };
    linux_memfd_install_ops(&ops);
}

/* -------- memfd_create -------- */

static void t_memfd_basic(void) {
    install_fake();
    const char *name = "shm-foo";
    int64_t r = linux_memfd_create((uint64_t)(uintptr_t)name, LINUX_MFD_CLOEXEC);
    TEST("memfd_create: basic returns fd >= LINUX_MEMFD_FD_BASE");
    if (r >= LINUX_MEMFD_FD_BASE && r < LINUX_MEMFD_FD_BASE + LINUX_MEMFD_MAX_INSTANCES)
        PASS();
    else FAIL("fd out of range");
}

static void t_memfd_unknown_flag(void) {
    install_fake();
    const char *name = "x";
    int64_t r = linux_memfd_create((uint64_t)(uintptr_t)name, 0x800u);
    TEST("memfd_create: unknown flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_memfd_null_name(void) {
    install_fake();
    int64_t r = linux_memfd_create(0, 0);
    TEST("memfd_create: NULL name -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_memfd_long_name(void) {
    install_fake();
    char name[300];
    for (size_t i = 0; i < sizeof(name) - 1; i++) name[i] = 'a';
    name[sizeof(name) - 1] = '\0';
    int64_t r = linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    TEST("memfd_create: name > 249 chars -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("long name accepted");
}

static void t_memfd_table_full(void) {
    install_fake();
    const char *name = "x";
    for (int i = 0; i < LINUX_MEMFD_MAX_INSTANCES; i++) {
        if (linux_memfd_create((uint64_t)(uintptr_t)name, 0) < 0) {
            FAIL("alloc failed early");
            return;
        }
    }
    int64_t r = linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    TEST("memfd_create: table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

/* -------- pidfd_open -------- */

static void t_pidfd_open_basic(void) {
    install_fake();
    int64_t r = linux_pidfd_open(42, 0);
    TEST("pidfd_open: existing pid returns fd in pidfd range");
    if (r >= LINUX_PIDFD_FD_BASE && r < LINUX_PIDFD_FD_BASE + LINUX_PIDFD_MAX_INSTANCES)
        PASS();
    else FAIL("fd out of range");
}

static void t_pidfd_open_pid_zero(void) {
    install_fake();
    int64_t r = linux_pidfd_open(0, 0);
    TEST("pidfd_open: pid=0 -> -EINVAL (Linux semantics)");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("pid=0 accepted");
}

static void t_pidfd_open_unknown_pid(void) {
    install_fake();
    int64_t r = linux_pidfd_open(99999, 0);
    TEST("pidfd_open: unknown pid -> -ESRCH");
    if (r == -LINUX_ESRCH) PASS();
    else FAIL("ESRCH not surfaced");
}

static void t_pidfd_open_unknown_flag(void) {
    install_fake();
    int64_t r = linux_pidfd_open(42, 0x1000u);
    TEST("pidfd_open: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("flag not validated");
}

static void t_pidfd_open_known_flag(void) {
    install_fake();
    int64_t r = linux_pidfd_open(42, LINUX_PIDFD_NONBLOCK);
    TEST("pidfd_open: PIDFD_NONBLOCK accepted");
    if (r >= 0) PASS();
    else FAIL("NONBLOCK rejected");
}

static void t_install_null_clears_pid_exists_for_open(void) {
    install_fake();
    linux_memfd_install_ops(NULL);
    int64_t r = linux_pidfd_open(99999, 0);
    TEST("memfd install_ops(NULL) clears pid_exists for pidfd_open");
    if (r >= LINUX_PIDFD_FD_BASE && g_pid_exists_calls == 0) PASS();
    else FAIL("pid_exists provider not cleared");
}

/* -------- pidfd_send_signal -------- */

static void t_send_signal_zero_probes_pid(void) {
    install_fake();
    int pidfd = (int)linux_pidfd_open(42, 0);
    int64_t r = linux_pidfd_send_signal(pidfd, 0, 0, 0);
    TEST("pidfd_send_signal sig=0: probe-only, returns 0 if pid alive");
    if (r == 0) PASS();
    else FAIL("probe failed");
}

static void t_send_signal_real_enosys(void) {
    install_fake();
    int pidfd = (int)linux_pidfd_open(42, 0);
    int64_t r = linux_pidfd_send_signal(pidfd, 9, 0, 0);
    TEST("pidfd_send_signal sig != 0: -ENOSYS until signal delivery lands");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("expected ENOSYS");
}

static void t_send_signal_bad_pidfd(void) {
    install_fake();
    int64_t r = linux_pidfd_send_signal(123, 0, 0, 0);
    TEST("pidfd_send_signal: unknown pidfd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_send_signal_bad_sig(void) {
    install_fake();
    int pidfd = (int)linux_pidfd_open(42, 0);
    int64_t r1 = linux_pidfd_send_signal(pidfd, -1, 0, 0);
    int64_t r2 = linux_pidfd_send_signal(pidfd, 65, 0, 0);
    TEST("pidfd_send_signal: sig out of [0,64] -> -EINVAL");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL) PASS();
    else FAIL("sig validation wrong");
}

static void t_send_signal_unknown_flag(void) {
    install_fake();
    int pidfd = (int)linux_pidfd_open(42, 0);
    int64_t r = linux_pidfd_send_signal(pidfd, 0, 0, 0x1u);
    TEST("pidfd_send_signal: any non-zero flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("flag not validated");
}

static void t_install_null_clears_pid_exists_for_probe(void) {
    install_fake();
    int pidfd = (int)linux_pidfd_open(42, 0);
    g_pid_exists_calls = 0;
    linux_memfd_install_ops(NULL);
    int64_t r = linux_pidfd_send_signal(pidfd, 0, 0, 0);
    TEST("memfd install_ops(NULL) clears pid_exists for pidfd sig=0 probe");
    if (r == 0 && g_pid_exists_calls == 0) PASS();
    else FAIL("pid_exists probe provider not cleared");
}

static void t_memfd_close_releases_slot(void) {
    install_fake();
    const char *name = "x";
    int64_t fd = linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    int64_t c = linux_memfd_family_close((int)fd);
    int64_t again = linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    TEST("memfd close releases fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("memfd slot not reused");
}

static void t_memfd_family_storage_ops(void) {
    install_fake();
    const char *name = "x";
    int fd = (int)linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    uint8_t buf[8];
    int64_t r = linux_memfd_family_read(fd, buf, sizeof(buf));
    int64_t w = linux_memfd_family_write(fd, "x", 1);
    int64_t s = linux_memfd_family_lseek(fd, 0, 0);
    TEST("memfd generic ops -> -ENOSYS until backing lands");
    if (r == -LINUX_ENOSYS && w == -LINUX_ENOSYS && s == -LINUX_ENOSYS) PASS();
    else FAIL("memfd generic ops not ENOSYS");
}

static void t_pidfd_close_releases_slot(void) {
    install_fake();
    int64_t fd = linux_pidfd_open(42, 0);
    int64_t c = linux_memfd_family_close((int)fd);
    int64_t again = linux_pidfd_open(100, 0);
    TEST("pidfd close releases fd slot for reuse");
    if (c == 0 && again == fd) PASS();
    else FAIL("pidfd slot not reused");
}

static void t_pidfd_family_ops(void) {
    install_fake();
    int fd = (int)linux_pidfd_open(42, 0);
    uint8_t buf[8];
    int64_t r = linux_memfd_family_read(fd, buf, sizeof(buf));
    int64_t w = linux_memfd_family_write(fd, "x", 1);
    int64_t s = linux_memfd_family_lseek(fd, 0, 0);
    TEST("pidfd generic ops -> read/write -EINVAL, lseek -ESPIPE");
    if (r == -LINUX_EINVAL && w == -LINUX_EINVAL && s == -LINUX_ESPIPE) PASS();
    else FAIL("pidfd generic op errno wrong");
}

static void t_memfd_family_bad_fd(void) {
    install_fake();
    uint8_t buf[8];
    int64_t r = linux_memfd_family_read(LINUX_MEMFD_FD_BASE, buf, sizeof(buf));
    TEST("memfd family read unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

int test_linux_memfd_run(void) {
    printf("[test_linux_memfd]\n");
    tests_run = tests_passed = 0;

    t_memfd_basic();
    t_memfd_unknown_flag();
    t_memfd_null_name();
    t_memfd_long_name();
    t_memfd_table_full();

    t_pidfd_open_basic();
    t_pidfd_open_pid_zero();
    t_pidfd_open_unknown_pid();
    t_pidfd_open_unknown_flag();
    t_pidfd_open_known_flag();
    t_install_null_clears_pid_exists_for_open();

    t_send_signal_zero_probes_pid();
    t_send_signal_real_enosys();
    t_send_signal_bad_pidfd();
    t_send_signal_bad_sig();
    t_send_signal_unknown_flag();
    t_install_null_clears_pid_exists_for_probe();
    t_memfd_close_releases_slot();
    t_memfd_family_storage_ops();
    t_pidfd_close_releases_slot();
    t_pidfd_family_ops();
    t_memfd_family_bad_fd();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
