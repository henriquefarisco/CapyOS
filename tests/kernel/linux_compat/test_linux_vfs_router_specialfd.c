/* Host tests for linux_vfs_router covering "special fd" subsystems
 * routed via the linux_vfs (end-to-end via linux_vfs).
 *
 * Carved out of `test_linux_vfs_router.c` at the 2026-05-16
 * preventive refactor when the parent reached 841/900 LOC. This
 * sibling TU covers the 34 router tests for the special-fd
 * families that all share the "fd created by a syscall + routed
 * through linux_vfs" pattern:
 *
 *   - eventfd / signalfd / timerfd       (6 tests)
 *   - memfd_secret + memfd family        (6 tests)
 *   - pidfd                              (2 tests)
 *   - inotify / epoll / fanotify         (12 tests)
 *   - userfaultfd                        (4 tests)
 *   - landlock ruleset                   (4 tests)
 *
 * Each TU carries its own private copy of the in-process fixture
 * helpers (`install_router`, `fake_csprng`, `router_meminfo`,
 * `router_cpuinfo`, `router_pid_exists`, `router_now_ns`,
 * `create_landlock_ruleset_fd`) and the
 * `tests_run`/`tests_passed` counters, all `static` so there is
 * no link-time collision with the parent TU.
 * The parent TU keeps the /dev, /proc, /tmp and prefix priority
 * test groups (35 tests).
 *
 * The runner `test_linux_vfs_router_specialfd_run` is declared and
 * invoked from `tests/test_runner.c` directly after the existing
 * `test_linux_vfs_router_run` entry.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_vfs_router.h"
#include "kernel/linux_compat/linux_devfs.h"
#include "kernel/linux_compat/linux_shm.h"
#include "kernel/linux_compat/linux_procfs.h"
#include "kernel/linux_compat/linux_tmpfs.h"
#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/linux_compat/linux_memfd.h"
#include "kernel/linux_compat/linux_modern_misc.h"
#include "kernel/linux_compat/linux_inotify.h"
#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_fanotify.h"
#include "kernel/linux_compat/linux_jit_aux.h"
#include "kernel/linux_compat/linux_landlock.h"
#include "kernel/linux_compat/linux_proc.h"
#include "kernel/linux_compat/linux_cpuinfo.h"
#include "kernel/linux_compat/linux_random.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;
static uint64_t g_router_now_ns;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static size_t g_csprng_calls;
static void fake_csprng(void *buf, size_t len) {
    g_csprng_calls++;
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) out[i] = 0xAB;
}

static int router_meminfo(struct linux_proc_meminfo *out) {
    out->mem_total_bytes = 1ull << 30;
    out->mem_free_bytes  = 1ull << 29;
    out->mem_available_bytes = 1ull << 29;
    return 0;
}
static size_t router_cpuinfo(struct linux_cpuinfo_entry *out, size_t cap) {
    if (cap == 0) return 0;
    out[0] = (struct linux_cpuinfo_entry){
        .processor_index = 0, .vendor_id = "Test", .model_name = "Test",
        .flags = LINUX_CPUINFO_FLAG_FPU,
    };
    return 1;
}
static int router_pid_exists(uint32_t pid) {
    return (pid == 42 || pid == 100) ? 1 : 0;
}

static void install_router(void) {
    linux_vfs_reset_for_tests();
    linux_devfs_reset_for_tests();
    linux_shm_reset_for_tests();
    linux_procfs_reset_for_tests();
    linux_tmpfs_reset_for_tests();
    linux_eventfd_reset_for_tests();
    linux_memfd_reset_for_tests();
    linux_modern_misc_reset_for_tests();
    linux_inotify_reset_for_tests();
    linux_epoll_reset_for_tests();
    linux_fanotify_reset_for_tests();
    linux_jit_aux_reset_for_tests();
    linux_landlock_reset_for_tests();
    linux_random_reset_for_tests();
    g_router_now_ns = 0;
    g_csprng_calls = 0;
    linux_random_install_source(fake_csprng);
    static const struct linux_procfs_providers procfs_ops = {
        .meminfo = router_meminfo,
        .cpuinfo = router_cpuinfo,
    };
    linux_procfs_install_providers(&procfs_ops);
    static const struct linux_memfd_ops memfd_ops = {
        .pid_exists = router_pid_exists,
    };
    linux_memfd_install_ops(&memfd_ops);
    linux_vfs_router_install();
}

static uint64_t router_now_ns(void) {
    return g_router_now_ns;
}

/* -------- eventfd/signalfd/timerfd via router -------- */

static void t_eventfd_read_via_router(void) {
    install_router();
    int fd = (int)linux_eventfd2(7, 0);
    uint64_t out = 0;
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)&out, sizeof(out));
    TEST("read eventfd via router returns counter and resets");
    if (r == 8 && out == 7) PASS();
    else FAIL("eventfd read not routed");
}

static void t_eventfd_write_via_router(void) {
    install_router();
    int fd = (int)linux_eventfd2(1, 0);
    uint64_t in = 4;
    int64_t w = linux_vfs_write(fd, (uint64_t)(uintptr_t)&in, sizeof(in));
    uint64_t out = 0;
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)&out, sizeof(out));
    TEST("write eventfd via router increments counter");
    if (w == 8 && r == 8 && out == 5) PASS();
    else FAIL("eventfd write not routed");
}

static void t_signalfd_read_via_router_eagain(void) {
    install_router();
    uint64_t mask = 1ull << 1;
    int fd = (int)linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    uint8_t info[LINUX_SIGNALFD_SIGINFO_SIZE];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)info, sizeof(info));
    TEST("read signalfd via router -> -EAGAIN until delivery lands");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("signalfd read not routed");
}

static void t_timerfd_read_via_router(void) {
    install_router();
    linux_eventfd_install_now_ns(router_now_ns);
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(fd, 0, &it, NULL);
    g_router_now_ns = 2000000000ull;
    uint64_t expirations = 0;
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)&expirations,
                               sizeof(expirations));
    TEST("read timerfd via router returns elapsed expirations");
    if (r == 8 && expirations == 1) PASS();
    else FAIL("timerfd read not routed");
}

static void t_eventfd_lseek_espipe(void) {
    install_router();
    int fd = (int)linux_eventfd2(0, 0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_CUR);
    TEST("lseek eventfd via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("ESPIPE not surfaced");
}

static void t_eventfd_close_via_router(void) {
    install_router();
    int fd = (int)linux_eventfd2(1, 0);
    int64_t c = linux_vfs_close(fd);
    uint64_t out = 0;
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)&out, sizeof(out));
    TEST("close eventfd via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("close not routed");
}

static void t_memfd_secret_read_via_router_enosys(void) {
    install_router();
    int fd = (int)linux_memfd_secret(0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read memfd_secret via router -> -ENOSYS until backing lands");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("memfd_secret read not routed");
}

static void t_memfd_secret_write_via_router_enosys(void) {
    install_router();
    int fd = (int)linux_memfd_secret(0);
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write memfd_secret via router -> -ENOSYS until backing lands");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("memfd_secret write not routed");
}

static void t_memfd_secret_lseek_via_router_enosys(void) {
    install_router();
    int fd = (int)linux_memfd_secret(0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek memfd_secret via router -> -ENOSYS until backing lands");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("memfd_secret lseek not routed");
}

static void t_memfd_secret_close_via_router(void) {
    install_router();
    int fd = (int)linux_memfd_secret(0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close memfd_secret via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("memfd_secret close not routed");
}

static void t_memfd_family_storage_ops_via_router(void) {
    install_router();
    const char *name = "x";
    int fd = (int)linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    int64_t w = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    int64_t s = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("memfd generic ops via router -> -ENOSYS until backing lands");
    if (r == -LINUX_ENOSYS && w == -LINUX_ENOSYS && s == -LINUX_ENOSYS) PASS();
    else FAIL("memfd family ops not routed");
}

static void t_memfd_family_close_via_router(void) {
    install_router();
    const char *name = "x";
    int fd = (int)linux_memfd_create((uint64_t)(uintptr_t)name, 0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close memfd via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("memfd close not routed");
}

static void t_pidfd_family_ops_via_router(void) {
    install_router();
    int fd = (int)linux_pidfd_open(42, 0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    int64_t w = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    int64_t s = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("pidfd generic ops via router -> read/write -EINVAL, lseek -ESPIPE");
    if (r == -LINUX_EINVAL && w == -LINUX_EINVAL && s == -LINUX_ESPIPE) PASS();
    else FAIL("pidfd family ops not routed");
}

static void t_pidfd_family_close_via_router(void) {
    install_router();
    int fd = (int)linux_pidfd_open(42, 0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close pidfd via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("pidfd close not routed");
}

static void t_inotify_read_via_router_eagain(void) {
    install_router();
    int fd = (int)linux_inotify_init1(0);
    uint8_t buf[16];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read inotify via router -> -EAGAIN until events land");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("inotify read not routed");
}

static void t_inotify_write_via_router_einval(void) {
    install_router();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write inotify via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("inotify write not routed");
}

static void t_inotify_lseek_via_router_espipe(void) {
    install_router();
    int fd = (int)linux_inotify_init1(0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek inotify via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("inotify lseek not routed");
}

static void t_inotify_close_via_router(void) {
    install_router();
    int fd = (int)linux_inotify_init1(0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close inotify via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("inotify close not routed");
}

static void t_epoll_read_via_router_einval(void) {
    install_router();
    int fd = (int)linux_epoll_create1(0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read epoll via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("epoll read not routed");
}

static void t_epoll_write_via_router_einval(void) {
    install_router();
    int fd = (int)linux_epoll_create1(0);
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write epoll via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("epoll write not routed");
}

static void t_epoll_lseek_via_router_espipe(void) {
    install_router();
    int fd = (int)linux_epoll_create1(0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek epoll via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("epoll lseek not routed");
}

static void t_epoll_close_via_router(void) {
    install_router();
    int fd = (int)linux_epoll_create1(0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close epoll via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("epoll close not routed");
}

static void t_fanotify_read_via_router_eagain(void) {
    install_router();
    int fd = (int)linux_fanotify_init(0, 0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read fanotify via router -> -EAGAIN until events land");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("fanotify read not routed");
}

static void t_fanotify_write_via_router_einval(void) {
    install_router();
    int fd = (int)linux_fanotify_init(0, 0);
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write fanotify via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("fanotify write not routed");
}

static void t_fanotify_lseek_via_router_espipe(void) {
    install_router();
    int fd = (int)linux_fanotify_init(0, 0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek fanotify via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("fanotify lseek not routed");
}

static void t_fanotify_close_via_router(void) {
    install_router();
    int fd = (int)linux_fanotify_init(0, 0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close fanotify via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("fanotify close not routed");
}

static void t_userfaultfd_read_via_router_eagain(void) {
    install_router();
    int fd = (int)linux_userfaultfd(0);
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read userfaultfd via router -> -EAGAIN until events land");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("userfaultfd read not routed");
}

static void t_userfaultfd_write_via_router_einval(void) {
    install_router();
    int fd = (int)linux_userfaultfd(0);
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write userfaultfd via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("userfaultfd write not routed");
}

static void t_userfaultfd_lseek_via_router_espipe(void) {
    install_router();
    int fd = (int)linux_userfaultfd(0);
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek userfaultfd via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("userfaultfd lseek not routed");
}

static void t_userfaultfd_close_via_router(void) {
    install_router();
    int fd = (int)linux_userfaultfd(0);
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close userfaultfd via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("userfaultfd close not routed");
}

static int create_landlock_ruleset_fd(void) {
    struct linux_landlock_ruleset_attr a = {
        .handled_access_fs = LINUX_LANDLOCK_ACCESS_FS_READ_FILE
    };
    return (int)linux_landlock_create_ruleset(&a, 16, 0);
}

static void t_landlock_read_via_router_einval(void) {
    install_router();
    int fd = create_landlock_ruleset_fd();
    uint8_t buf[8];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("read landlock ruleset via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("landlock read not routed");
}

static void t_landlock_write_via_router_einval(void) {
    install_router();
    int fd = create_landlock_ruleset_fd();
    int64_t r = linux_vfs_write(fd, (uint64_t)(uintptr_t)"x", 1);
    TEST("write landlock ruleset via router -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("landlock write not routed");
}

static void t_landlock_lseek_via_router_espipe(void) {
    install_router();
    int fd = create_landlock_ruleset_fd();
    int64_t r = linux_vfs_lseek(fd, 0, LINUX_SEEK_SET);
    TEST("lseek landlock ruleset via router -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("landlock lseek not routed");
}

static void t_landlock_close_via_router(void) {
    install_router();
    int fd = create_landlock_ruleset_fd();
    int64_t c = linux_vfs_close(fd);
    uint8_t buf[1];
    int64_t r = linux_vfs_read(fd, (uint64_t)(uintptr_t)buf, sizeof(buf));
    TEST("close landlock ruleset via router releases fd slot");
    if (c == 0 && r == -LINUX_EBADF) PASS();
    else FAIL("landlock close not routed");
}

int test_linux_vfs_router_specialfd_run(void) {
    printf("[test_linux_vfs_router_specialfd]\n");
    tests_run = tests_passed = 0;

    t_eventfd_read_via_router();
    t_eventfd_write_via_router();
    t_signalfd_read_via_router_eagain();
    t_timerfd_read_via_router();
    t_eventfd_lseek_espipe();
    t_eventfd_close_via_router();
    t_memfd_secret_read_via_router_enosys();
    t_memfd_secret_write_via_router_enosys();
    t_memfd_secret_lseek_via_router_enosys();
    t_memfd_secret_close_via_router();
    t_memfd_family_storage_ops_via_router();
    t_memfd_family_close_via_router();
    t_pidfd_family_ops_via_router();
    t_pidfd_family_close_via_router();
    t_inotify_read_via_router_eagain();
    t_inotify_write_via_router_einval();
    t_inotify_lseek_via_router_espipe();
    t_inotify_close_via_router();
    t_epoll_read_via_router_einval();
    t_epoll_write_via_router_einval();
    t_epoll_lseek_via_router_espipe();
    t_epoll_close_via_router();
    t_fanotify_read_via_router_eagain();
    t_fanotify_write_via_router_einval();
    t_fanotify_lseek_via_router_espipe();
    t_fanotify_close_via_router();
    t_userfaultfd_read_via_router_eagain();
    t_userfaultfd_write_via_router_einval();
    t_userfaultfd_lseek_via_router_espipe();
    t_userfaultfd_close_via_router();
    t_landlock_read_via_router_einval();
    t_landlock_write_via_router_einval();
    t_landlock_lseek_via_router_espipe();
    t_landlock_close_via_router();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
