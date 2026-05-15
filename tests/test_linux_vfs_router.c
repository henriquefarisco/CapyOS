/* Host tests for linux_vfs_router (end-to-end via linux_vfs).
 *
 * These exercise the real router with the real backends
 * (linux_devfs + linux_shm) installed, so they validate that
 * open("/dev/urandom") actually returns bytes from the CSPRNG
 * source, etc. The router has no state of its own so we only
 * reset the backends between tests.
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

/* CSPRNG fake for /dev/urandom paths. Counts bytes requested
 * so tests can assert delegation; fills with a recognisable
 * pattern (0xAB) so the byte stream is verifiable. */
static size_t g_csprng_calls;
static void fake_csprng(void *buf, size_t len) {
    g_csprng_calls++;
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) out[i] = 0xAB;
}

/* Minimal procfs providers for router tests. */
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

static int64_t do_open(const char *p, uint32_t flags, uint32_t mode) {
    return linux_vfs_open((uint64_t)(uintptr_t)p, flags, mode);
}

/* -------- /dev/<x> via router -------- */

static void t_open_dev_null(void) {
    install_router();
    int64_t fd = do_open("/dev/null", LINUX_VFS_O_RDONLY, 0);
    TEST("open /dev/null routes to devfs (fd in devfs range)");
    if (fd >= LINUX_DEVFS_FD_BASE &&
        fd <  LINUX_DEVFS_FD_BASE + LINUX_DEVFS_MAX_INSTANCES) PASS();
    else FAIL("fd out of range");
}

static void t_open_dev_unknown_enoent(void) {
    install_router();
    int64_t fd = do_open("/dev/totally-fake", LINUX_VFS_O_RDONLY, 0);
    TEST("open /dev/<unknown> -> -ENOENT (router preserves errno)");
    if (fd == -LINUX_ENOENT) PASS(); else FAIL("ENOENT not surfaced");
}

static void t_open_unknown_prefix_enoent(void) {
    install_router();
    int64_t fd = do_open("/etc/hosts", LINUX_VFS_O_RDONLY, 0);
    TEST("open outside /dev/ -> -ENOENT (no /tmp routing yet)");
    if (fd == -LINUX_ENOENT) PASS(); else FAIL("ENOENT not surfaced");
}

static void t_dev_zero_read_fills(void) {
    install_router();
    int64_t fd = do_open("/dev/zero", LINUX_VFS_O_RDONLY, 0);
    if (fd < 0) { TEST("open /dev/zero"); FAIL("could not open"); return; }

    uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int64_t r = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, 8);
    int all_zero = 1;
    for (int i = 0; i < 8; i++) if (buf[i] != 0) { all_zero = 0; break; }
    TEST("read /dev/zero fills buffer with 0x00 (8 bytes)");
    if (r == 8 && all_zero) PASS();
    else FAIL("zero fill wrong");
}

static void t_dev_urandom_read_delegates(void) {
    install_router();
    int64_t fd = do_open("/dev/urandom", LINUX_VFS_O_RDONLY, 0);
    if (fd < 0) { TEST("open /dev/urandom"); FAIL("could not open"); return; }

    uint8_t buf[8] = {0};
    int64_t r = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, 8);
    int all_pattern = 1;
    for (int i = 0; i < 8; i++) if (buf[i] != 0xAB) { all_pattern = 0; break; }
    TEST("read /dev/urandom delegates to CSPRNG source");
    if (r == 8 && g_csprng_calls == 1 && all_pattern) PASS();
    else FAIL("CSPRNG not called or wrong bytes");
}

static void t_dev_full_write_enospc(void) {
    install_router();
    int64_t fd = do_open("/dev/full", LINUX_VFS_O_WRONLY, 0);
    if (fd < 0) { TEST("open /dev/full"); FAIL("could not open"); return; }

    uint8_t buf[4] = {1, 2, 3, 4};
    int64_t r = linux_vfs_write((int)fd, (uint64_t)(uintptr_t)buf, 4);
    TEST("write /dev/full -> -ENOSPC end-to-end");
    if (r == -LINUX_ENOSPC) PASS();
    else FAIL("ENOSPC not surfaced");
}

static void t_dev_null_write_swallows(void) {
    install_router();
    int64_t fd = do_open("/dev/null", LINUX_VFS_O_WRONLY, 0);
    if (fd < 0) { TEST("open /dev/null"); FAIL("could not open"); return; }

    uint8_t buf[16] = {0};
    int64_t r = linux_vfs_write((int)fd, (uint64_t)(uintptr_t)buf, 16);
    TEST("write /dev/null returns count (sink semantics)");
    if (r == 16) PASS();
    else FAIL("count not returned");
}

static void t_dev_lseek_returns_zero(void) {
    install_router();
    int64_t fd = do_open("/dev/zero", LINUX_VFS_O_RDONLY, 0);
    if (fd < 0) { TEST("open /dev/zero"); FAIL("could not open"); return; }

    int64_t r = linux_vfs_lseek((int)fd, 100, LINUX_SEEK_SET);
    TEST("lseek /dev/zero always returns 0 (char device)");
    if (r == 0) PASS();
    else FAIL("non-zero pos returned");
}

static void t_dev_close_releases_slot(void) {
    install_router();
    int64_t fd1 = do_open("/dev/zero", LINUX_VFS_O_RDONLY, 0);
    int64_t cr = linux_vfs_close((int)fd1);
    int64_t fd2 = do_open("/dev/zero", LINUX_VFS_O_RDONLY, 0);
    TEST("close /dev/zero frees slot for re-open");
    if (cr == 0 && fd2 == fd1) PASS();
    else FAIL("slot not reused");
}

/* -------- /dev/shm/ via router -------- */

static void t_open_dev_shm_routes(void) {
    install_router();
    int64_t fd = do_open("/dev/shm/foo",
                         LINUX_VFS_O_CREAT | LINUX_VFS_O_RDWR, 0600);
    TEST("open /dev/shm/foo routes to shm (fd in shm range)");
    if (fd >= LINUX_SHM_FD_BASE) PASS();
    else FAIL("fd out of shm range");
}

static void t_dev_shm_empty_einval(void) {
    install_router();
    int64_t fd = do_open("/dev/shm/", LINUX_VFS_O_CREAT, 0);
    TEST("open /dev/shm/ (empty name) -> -EINVAL");
    if (fd == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_dev_shm_close(void) {
    install_router();
    int64_t fd = do_open("/dev/shm/bar", LINUX_VFS_O_CREAT, 0);
    int64_t r = linux_vfs_close((int)fd);
    TEST("close /dev/shm fd dispatches to shm_close (returns 0)");
    if (r == 0) PASS();
    else FAIL("close failed");
}

static void t_dev_shm_read_enosys(void) {
    install_router();
    int64_t fd = do_open("/dev/shm/baz", LINUX_VFS_O_CREAT, 0);
    uint8_t buf[4];
    int64_t r = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, 4);
    TEST("read /dev/shm fd -> -ENOSYS (mmap-only until backing pages)");
    if (r == -LINUX_ENOSYS) PASS();
    else FAIL("ENOSYS not surfaced");
}

static void t_dev_shm_lseek_returns_zero(void) {
    install_router();
    int64_t fd = do_open("/dev/shm/x", LINUX_VFS_O_CREAT, 0);
    int64_t r = linux_vfs_lseek((int)fd, 50, LINUX_SEEK_SET);
    TEST("lseek /dev/shm fd: returns 0 (no per-fd position yet)");
    if (r == 0) PASS();
    else FAIL("non-zero or error");
}

/* -------- fd dispatch boundaries -------- */

static void t_unknown_fd_close_ebadf(void) {
    install_router();
    int64_t r = linux_vfs_close(0xDEAD);
    TEST("close unknown fd (outside any backend) -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_unknown_fd_read_ebadf(void) {
    install_router();
    uint8_t buf[4];
    int64_t r = linux_vfs_read(0xDEAD, (uint64_t)(uintptr_t)buf, 4);
    TEST("read unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* -------- /proc/<x> via router -------- */

static void t_open_proc_meminfo_routes(void) {
    install_router();
    int64_t fd = do_open("/proc/meminfo", LINUX_VFS_O_RDONLY, 0);
    TEST("open /proc/meminfo routes to procfs (fd in procfs range)");
    if (fd >= LINUX_PROCFS_FD_BASE &&
        fd <  LINUX_PROCFS_FD_BASE + LINUX_PROCFS_MAX_INSTANCES) PASS();
    else FAIL("fd out of procfs range");
}

static void t_proc_cpuinfo_read(void) {
    install_router();
    int64_t fd = do_open("/proc/cpuinfo", LINUX_VFS_O_RDONLY, 0);
    if (fd < 0) { TEST("open /proc/cpuinfo"); FAIL("open"); return; }

    char buf[2048] = {0};
    int64_t n = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, sizeof(buf) - 1);
    int has_fpu = (n > 0 && strstr(buf, "fpu") != NULL);
    TEST("read /proc/cpuinfo via router emits 'fpu' flag");
    if (has_fpu) PASS();
    else FAIL("flag missing");
}

static void t_proc_unknown_path(void) {
    install_router();
    int64_t fd = do_open("/proc/sys/kernel/random", LINUX_VFS_O_RDONLY, 0);
    TEST("open /proc/sys/... (unsupported) -> -ENOENT");
    if (fd == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_proc_write_erofs(void) {
    install_router();
    int64_t fd = do_open("/proc/meminfo", LINUX_VFS_O_RDONLY, 0);
    char b[4] = {1};
    int64_t r = linux_vfs_write((int)fd, (uint64_t)(uintptr_t)b, 4);
    TEST("write /proc/meminfo via router -> -EROFS");
    if (r == -LINUX_EROFS) PASS();
    else FAIL("EROFS not surfaced");
}

static void t_proc_lseek_works(void) {
    install_router();
    int64_t fd = do_open("/proc/meminfo", LINUX_VFS_O_RDONLY, 0);
    int64_t end = linux_vfs_lseek((int)fd, 0, LINUX_SEEK_END);
    int64_t r = linux_vfs_lseek((int)fd, 0, LINUX_SEEK_SET);
    TEST("lseek SEEK_END returns size, SEEK_SET returns 0");
    if (end > 0 && r == 0) PASS();
    else FAIL("lseek wrong");
}

static void t_proc_close(void) {
    install_router();
    int64_t fd = do_open("/proc/meminfo", LINUX_VFS_O_RDONLY, 0);
    int64_t r = linux_vfs_close((int)fd);
    TEST("close /proc/meminfo fd dispatches to procfs_close");
    if (r == 0) PASS();
    else FAIL("close failed");
}

/* -------- /tmp/<x> via router -------- */

static void t_open_tmp_creates(void) {
    install_router();
    int64_t fd = do_open("/tmp/foo",
                         LINUX_VFS_O_CREAT | LINUX_VFS_O_RDWR, 0644);
    TEST("open /tmp/foo with O_CREAT routes to tmpfs (fd in tmpfs range)");
    if (fd >= LINUX_TMPFS_FD_BASE &&
        fd <  LINUX_TMPFS_FD_BASE + LINUX_TMPFS_MAX_HANDLES) PASS();
    else FAIL("fd out of tmpfs range");
}

static void t_tmp_write_then_read(void) {
    install_router();
    int64_t fd = do_open("/tmp/rw",
                         LINUX_VFS_O_CREAT | LINUX_VFS_O_RDWR, 0644);
    if (fd < 0) { TEST("open /tmp/rw"); FAIL("open"); return; }
    int64_t w = linux_vfs_write((int)fd,
                                (uint64_t)(uintptr_t)"hello", 5);
    linux_vfs_lseek((int)fd, 0, LINUX_SEEK_SET);
    char buf[8] = {0};
    int64_t r = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, 8);
    TEST("write 'hello' then read returns same bytes via router");
    if (w == 5 && r == 5 && memcmp(buf, "hello", 5) == 0) PASS();
    else FAIL("roundtrip wrong");
}

static void t_tmp_no_create_enoent(void) {
    install_router();
    int64_t fd = do_open("/tmp/none", LINUX_VFS_O_RDONLY, 0);
    TEST("open /tmp/none without O_CREAT -> -ENOENT (router preserves)");
    if (fd == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_tmp_lseek_then_read(void) {
    install_router();
    int64_t fd = do_open("/tmp/seek",
                         LINUX_VFS_O_CREAT | LINUX_VFS_O_RDWR, 0);
    linux_vfs_write((int)fd, (uint64_t)(uintptr_t)"abcdef", 6);
    int64_t end = linux_vfs_lseek((int)fd, 0, LINUX_SEEK_END);
    int64_t pos = linux_vfs_lseek((int)fd, 2, LINUX_SEEK_SET);
    char buf[8] = {0};
    int64_t r = linux_vfs_read((int)fd, (uint64_t)(uintptr_t)buf, 4);
    TEST("lseek to 2 then read 4 bytes returns 'cdef'");
    if (end == 6 && pos == 2 && r == 4 && memcmp(buf, "cdef", 4) == 0) PASS();
    else FAIL("lseek/read wrong");
}

static void t_tmp_close_releases(void) {
    install_router();
    int64_t fd = do_open("/tmp/c",
                         LINUX_VFS_O_CREAT | LINUX_VFS_O_RDWR, 0);
    int64_t r = linux_vfs_close((int)fd);
    TEST("close /tmp fd dispatches to tmpfs_close (returns 0)");
    if (r == 0) PASS();
    else FAIL("close failed");
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

/* -------- prefix order regression -------- */

static void t_dev_shm_prefix_priority(void) {
    /* Regression guard: /dev/shm/ MUST be matched before /dev/ so
     * that the router doesn't try to open shm paths via devfs (which
     * would surface ENOENT). */
    install_router();
    int64_t fd1 = do_open("/dev/shm/quux", LINUX_VFS_O_CREAT, 0);
    int64_t fd2 = do_open("/dev/null", LINUX_VFS_O_RDONLY, 0);
    TEST("router prefix order: /dev/shm/ checked before /dev/");
    if (fd1 >= LINUX_SHM_FD_BASE &&
        fd2 >= LINUX_DEVFS_FD_BASE && fd2 < LINUX_SHM_FD_BASE) PASS();
    else FAIL("prefix priority wrong");
}

int test_linux_vfs_router_run(void) {
    printf("[test_linux_vfs_router]\n");
    tests_run = tests_passed = 0;

    t_open_dev_null();
    t_open_dev_unknown_enoent();
    t_open_unknown_prefix_enoent();
    t_dev_zero_read_fills();
    t_dev_urandom_read_delegates();
    t_dev_full_write_enospc();
    t_dev_null_write_swallows();
    t_dev_lseek_returns_zero();
    t_dev_close_releases_slot();

    t_open_dev_shm_routes();
    t_dev_shm_empty_einval();
    t_dev_shm_close();
    t_dev_shm_read_enosys();
    t_dev_shm_lseek_returns_zero();

    t_unknown_fd_close_ebadf();
    t_unknown_fd_read_ebadf();

    t_open_proc_meminfo_routes();
    t_proc_cpuinfo_read();
    t_proc_unknown_path();
    t_proc_write_erofs();
    t_proc_lseek_works();
    t_proc_close();

    t_open_tmp_creates();
    t_tmp_write_then_read();
    t_tmp_no_create_enoent();
    t_tmp_lseek_then_read();
    t_tmp_close_releases();

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

    t_dev_shm_prefix_priority();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
