/*
 * Host tests for linux_eventfd (S1.7).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_signal.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int      g_alloc_fd_calls;
static int      g_last_alloc_slot;
static uint32_t g_last_alloc_flags;

static int fake_alloc_fd(int slot, uint32_t flags) {
    g_alloc_fd_calls++;
    g_last_alloc_slot = slot;
    g_last_alloc_flags = flags;
    return 0x4f00 + slot;
}

static void install_fake_alloc_fd(void) {
    static const struct linux_eventfd_ops ops = {
        .alloc_fd = fake_alloc_fd,
    };
    g_alloc_fd_calls = 0;
    g_last_alloc_slot = -1;
    g_last_alloc_flags = 0;
    linux_eventfd_reset_for_tests();
    linux_eventfd_install_ops(&ops);
}

/* -------- eventfd2 -------- */

static void t_eventfd2_basic(void) {
    linux_eventfd_reset_for_tests();
    int64_t fd = linux_eventfd2(0, 0);
    TEST("eventfd2: basic returns fd >= LINUX_EVENTFD_FD_BASE");
    if (fd >= LINUX_EVENTFD_FD_BASE) PASS();
    else FAIL("fd outside expected range");
}

static void t_eventfd2_unknown_flag(void) {
    linux_eventfd_reset_for_tests();
    int64_t fd = linux_eventfd2(0, 0x1000000u);
    TEST("eventfd2: unknown flag bit -> -EINVAL");
    if (fd == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_eventfd2_known_flags(void) {
    linux_eventfd_reset_for_tests();
    int64_t f1 = linux_eventfd2(0, LINUX_EFD_CLOEXEC);
    int f1_ok = f1 >= 0;
    int64_t f2 = linux_eventfd2(0, LINUX_EFD_NONBLOCK);
    int f2_ok = f2 >= 0;
    int64_t f3 = linux_eventfd2(0, LINUX_EFD_SEMAPHORE);
    int f3_ok = f3 >= 0;
    int64_t f4 = linux_eventfd2(0, LINUX_EFD_CLOEXEC|LINUX_EFD_NONBLOCK|LINUX_EFD_SEMAPHORE);
    int f4_ok = f4 >= 0;
    TEST("eventfd2: CLOEXEC/NONBLOCK/SEMAPHORE (and combo) accepted");
    if (f1_ok && f2_ok && f3_ok && f4_ok) PASS();
    else FAIL("known flag rejected");
}

static void t_eventfd2_table_full(void) {
    linux_eventfd_reset_for_tests();
    /* fill the slot table */
    for (int i = 0; i < LINUX_EVENTFD_MAX; i++) {
        if (linux_eventfd2(0, 0) < 0) { FAIL("alloc failed early"); return; }
    }
    int64_t r = linux_eventfd2(0, 0);
    TEST("eventfd2: slot table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

static void t_eventfd_install_null_clears_alloc_fd(void) {
    install_fake_alloc_fd();
    linux_eventfd_install_ops(NULL);
    int64_t fd = linux_eventfd2(0, LINUX_EFD_CLOEXEC);
    TEST("eventfd install_ops(NULL) clears alloc_fd callback");
    if (fd == LINUX_EVENTFD_FD_BASE && g_alloc_fd_calls == 0) PASS();
    else FAIL("stale alloc_fd invoked");
}

static void t_eventfd_reset_clears_alloc_fd(void) {
    install_fake_alloc_fd();
    linux_eventfd_reset_for_tests();
    int64_t fd = linux_eventfd2(0, LINUX_EFD_CLOEXEC);
    TEST("eventfd reset clears installed callbacks");
    if (fd == LINUX_EVENTFD_FD_BASE && g_alloc_fd_calls == 0) PASS();
    else FAIL("stale alloc_fd invoked");
}

/* -------- read -------- */

static void t_read_basic_reset(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(7, 0);
    uint64_t v;
    int64_t r = linux_eventfd_read(fd, &v, sizeof(v));
    TEST("eventfd read: returns counter (7), resets to 0");
    if (r == 8 && v == 7) PASS();
    else FAIL("read wrong");
}

static void t_read_after_reset_eagain(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(0, 0);
    uint64_t v;
    int64_t r = linux_eventfd_read(fd, &v, sizeof(v));
    TEST("eventfd read: counter == 0 -> -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("EAGAIN not surfaced");
}

static void t_read_semaphore_mode(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(3, LINUX_EFD_SEMAPHORE);
    uint64_t v;
    /* 1st read: returns 1, counter -> 2 */
    int64_t r1 = linux_eventfd_read(fd, &v, sizeof(v));
    int ok1 = (r1 == 8 && v == 1);
    /* 2nd read: 1, counter -> 1 */
    int64_t r2 = linux_eventfd_read(fd, &v, sizeof(v));
    int ok2 = (r2 == 8 && v == 1);
    /* 3rd read: 1, counter -> 0 */
    int64_t r3 = linux_eventfd_read(fd, &v, sizeof(v));
    int ok3 = (r3 == 8 && v == 1);
    /* 4th read: EAGAIN */
    int64_t r4 = linux_eventfd_read(fd, &v, sizeof(v));
    int ok4 = (r4 == -LINUX_EAGAIN);
    TEST("eventfd read SEMAPHORE: returns 1 each call until counter drains");
    if (ok1 && ok2 && ok3 && ok4) PASS();
    else FAIL("semaphore semantics wrong");
}

static void t_read_short_buf(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(1, 0);
    uint64_t v;
    int64_t r = linux_eventfd_read(fd, &v, 4);
    TEST("eventfd read: len < 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("short read not rejected");
}

static void t_read_null_buf(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(1, 0);
    int64_t r = linux_eventfd_read(fd, NULL, 8);
    TEST("eventfd read: NULL buf -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_read_bad_fd(void) {
    linux_eventfd_reset_for_tests();
    uint64_t v;
    int64_t r = linux_eventfd_read(123, &v, sizeof(v));
    TEST("eventfd read: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

/* -------- write -------- */

static void t_write_basic_increments(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(5, 0);
    int64_t r = linux_eventfd_write(fd, 10, sizeof(uint64_t));
    uint64_t v;
    linux_eventfd_read(fd, &v, sizeof(v));
    TEST("eventfd write: adds value to counter");
    if (r == 8 && v == 15) PASS();
    else FAIL("counter not incremented");
}

static void t_write_sentinel_einval(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(0, 0);
    int64_t r = linux_eventfd_write(fd, (uint64_t)-1, sizeof(uint64_t));
    TEST("eventfd write: 0xFFFFFFFFFFFFFFFF (sentinel) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("sentinel accepted");
}

static void t_write_short_buf(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(0, 0);
    int64_t r = linux_eventfd_write(fd, 1, 4);
    TEST("eventfd write: len < 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("short write not rejected");
}

static void t_write_overflow_eagain(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2((uint64_t)-100, 0);
    int64_t r = linux_eventfd_write(fd, 1000, sizeof(uint64_t));
    TEST("eventfd write: counter overflow -> -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("overflow not detected");
}

static void t_write_bad_fd(void) {
    linux_eventfd_reset_for_tests();
    int64_t r = linux_eventfd_write(99, 1, sizeof(uint64_t));
    TEST("eventfd write: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_poll_eventfd_read_write_readiness(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(0, LINUX_EFD_NONBLOCK);
    uint32_t r0 = linux_eventfd_family_poll_events(fd);
    linux_eventfd_write(fd, 1, sizeof(uint64_t));
    uint32_t r1 = linux_eventfd_family_poll_events(fd);
    uint64_t value = 0;
    int64_t rr = linux_eventfd_read(fd, &value, sizeof(value));
    uint32_t r2 = linux_eventfd_family_poll_events(fd);
    TEST("eventfd poll: EPOLLIN/EPOLLOUT follows counter state");
    if (r0 == LINUX_EPOLLOUT &&
        r1 == (LINUX_EPOLLIN | LINUX_EPOLLOUT) &&
        rr == 8 && value == 1 && r2 == LINUX_EPOLLOUT) PASS();
    else FAIL("eventfd poll readiness wrong");
}

static void t_poll_eventfd_saturated_not_writable(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2((uint64_t)-2, LINUX_EFD_NONBLOCK);
    uint32_t ready = linux_eventfd_family_poll_events(fd);
    int64_t wr = linux_eventfd_write(fd, 1, sizeof(uint64_t));
    TEST("eventfd poll: saturated counter does not report EPOLLOUT");
    if (ready == LINUX_EPOLLIN && wr == -LINUX_EAGAIN) PASS();
    else FAIL("saturated eventfd reported writable");
}

static void t_poll_eventfd_semaphore_drains_readiness(void) {
    linux_eventfd_reset_for_tests();
    int fd = (int)linux_eventfd2(2, LINUX_EFD_SEMAPHORE | LINUX_EFD_NONBLOCK);
    uint32_t r0 = linux_eventfd_family_poll_events(fd);
    uint64_t value = 0;
    int64_t rr1 = linux_eventfd_read(fd, &value, sizeof(value));
    uint32_t r1 = linux_eventfd_family_poll_events(fd);
    int64_t rr2 = linux_eventfd_read(fd, &value, sizeof(value));
    uint32_t r2 = linux_eventfd_family_poll_events(fd);
    TEST("eventfd poll: semaphore mode keeps EPOLLIN until counter drains");
    if (r0 == (LINUX_EPOLLIN | LINUX_EPOLLOUT) &&
        rr1 == 8 && r1 == (LINUX_EPOLLIN | LINUX_EPOLLOUT) &&
        rr2 == 8 && r2 == LINUX_EPOLLOUT) PASS();
    else FAIL("semaphore poll readiness wrong");
}

/* -------- signalfd4 storage-only -------- */

static void t_signalfd4_unknown_flag(void) {
    int64_t r = linux_signalfd4(-1, 0, 0, 0x1000u);
    TEST("signalfd4: unknown flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_signalfd4_null_mask_efault(void) {
    linux_eventfd_reset_for_tests();
    int64_t r = linux_signalfd4(-1, 0, 8, 0);
    TEST("signalfd4: NULL mask pointer -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("EFAULT not surfaced");
}

static void t_signalfd4_bad_sigset_size(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int64_t r = linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 16, 0);
    TEST("signalfd4: sizemask != 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_signalfd4_create_fd(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int64_t fd = linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8,
                                 LINUX_SFD_CLOEXEC);
    TEST("signalfd4: create storage fd in signalfd range");
    if (fd >= LINUX_SIGNALFD_FD_BASE &&
        fd < LINUX_SIGNALFD_FD_BASE + LINUX_SIGNALFD_MAX) PASS();
    else FAIL("fd outside signalfd range");
}

static void t_signalfd4_update_existing_fd(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int64_t fd = linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    mask = 1ull << (LINUX_SIGTERM - 1);
    int64_t r = linux_signalfd4((int)fd, (uint64_t)(uintptr_t)&mask, 8,
                                LINUX_SFD_NONBLOCK);
    TEST("signalfd4: existing signalfd updates mask and returns same fd");
    if (fd >= 0 && r == fd) PASS();
    else FAIL("update did not return same fd");
}

static void t_signalfd4_eventfd_update_einval(void) {
    linux_eventfd_reset_for_tests();
    int efd = (int)linux_eventfd2(0, 0);
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int64_t r = linux_signalfd4(efd, (uint64_t)(uintptr_t)&mask, 8, 0);
    TEST("signalfd4: updating non-signalfd eventfd -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_signalfd_read_no_pending_eagain(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int fd = (int)linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    uint8_t info[LINUX_SIGNALFD_SIGINFO_SIZE];
    int64_t r = linux_signalfd_read(fd, info, sizeof(info));
    TEST("signalfd read: no delivered signals yet -> -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("EAGAIN not surfaced");
}

static void t_signalfd_read_short_einval(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int fd = (int)linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    uint8_t info[LINUX_SIGNALFD_SIGINFO_SIZE];
    int64_t r = linux_signalfd_read(fd, info,
                                    LINUX_SIGNALFD_SIGINFO_SIZE - 1);
    TEST("signalfd read: len < sizeof(signalfd_siginfo) -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("short read not rejected");
}

static void t_signalfd4_table_full(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    for (int i = 0; i < LINUX_SIGNALFD_MAX; i++) {
        if (linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0) < 0) {
            FAIL("signalfd alloc failed early");
            return;
        }
    }
    int64_t r = linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    TEST("signalfd4: table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

static void t_poll_signalfd_no_readiness(void) {
    linux_eventfd_reset_for_tests();
    uint64_t mask = 1ull << (LINUX_SIGINT - 1);
    int fd = (int)linux_signalfd4(-1, (uint64_t)(uintptr_t)&mask, 8, 0);
    uint32_t ready = linux_eventfd_family_poll_events(fd);
    TEST("signalfd poll: storage-only fd has no readiness");
    if (fd >= LINUX_SIGNALFD_FD_BASE && ready == 0) PASS();
    else FAIL("signalfd unexpectedly ready");
}

static void t_timerfd_create_bad_clock(void) {
    int64_t r = linux_timerfd_create(99, 0);
    TEST("timerfd_create: unknown clockid -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("clockid not validated");
}

static void t_timerfd_create_bad_flag(void) {
    int64_t r = linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0x1000u);
    TEST("timerfd_create: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("flag not validated");
}

/* timerfd functional tests rely on a deterministic clock. */
static uint64_t g_test_now_ns;
static uint64_t test_now_ns(void) { return g_test_now_ns; }

static void install_timerfd_clock(void) {
    linux_eventfd_install_now_ns(test_now_ns);
}

static void t_timerfd_create_known(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    int64_t r1 = linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    TEST("timerfd_create: known clockid -> fd >= LINUX_TIMERFD_FD_BASE");
    if (r1 >= LINUX_TIMERFD_FD_BASE &&
        r1 < LINUX_TIMERFD_FD_BASE + LINUX_TIMERFD_MAX_INSTANCES) PASS();
    else FAIL("fd out of range");
}

static void t_timerfd_settime_oneshot(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 1000;  /* 1us into clock */

    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    /* Arm 1 second from now, no interval. */
    struct linux_itimerspec it = {
        .it_value_sec = 1, .it_value_nsec = 0,
    };
    int64_t r = linux_timerfd_settime(fd, 0, &it, NULL);
    TEST("timerfd_settime: arm one-shot timer 1s out, returns 0");
    if (r == 0) PASS(); else FAIL("settime failed");
}

static void t_timerfd_read_before_expiry(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(fd, 0, &it, NULL);

    g_test_now_ns = 500000000ull;  /* 0.5s, before expiry */
    uint64_t exp;
    int64_t r = linux_timerfd_read(fd, &exp, sizeof(exp));
    TEST("timerfd_read: before expiry -> -EAGAIN");
    if (r == -LINUX_EAGAIN) PASS(); else FAIL("expected EAGAIN");
}

static void t_timerfd_read_after_expiry(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(fd, 0, &it, NULL);

    g_test_now_ns = 2000000000ull;  /* 2s, well past 1s expiry */
    uint64_t exp;
    int64_t r = linux_timerfd_read(fd, &exp, sizeof(exp));
    TEST("timerfd_read after one-shot expiry -> 1 expiration, disarms");
    /* After one-shot reads, expires_at_ns goes to 0 and a second
     * read yields EAGAIN. */
    int64_t r2 = linux_timerfd_read(fd, &exp, sizeof(exp));
    if (r == 8 && exp == 1 && r2 == -LINUX_EAGAIN) PASS();
    else FAIL("one-shot semantics wrong");
}

static void t_timerfd_periodic(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    /* 100ms initial, 100ms interval. */
    struct linux_itimerspec it = {
        .it_value_nsec    = 100000000ull,
        .it_interval_nsec = 100000000ull,
    };
    linux_timerfd_settime(fd, 0, &it, NULL);

    /* Jump 350ms forward: expect 3 expirations (at 100, 200, 300). */
    g_test_now_ns = 350000000ull;
    uint64_t exp;
    int64_t r = linux_timerfd_read(fd, &exp, sizeof(exp));
    TEST("timerfd_read periodic: counts elapsed expirations (3 in 350ms)");
    if (r == 8 && exp == 3) PASS();
    else FAIL("periodic count wrong");
}

static void t_timerfd_gettime(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    struct linux_itimerspec it = {
        .it_value_sec = 1, .it_interval_nsec = 250000000ull,
    };
    linux_timerfd_settime(fd, 0, &it, NULL);

    g_test_now_ns = 250000000ull;  /* 250ms in */
    struct linux_itimerspec cur = {0};
    int64_t r = linux_timerfd_gettime(fd, &cur);
    TEST("timerfd_gettime: returns remaining (0.75s) and interval");
    if (r == 0 &&
        cur.it_value_sec == 0 && cur.it_value_nsec == 750000000ull &&
        cur.it_interval_nsec == 250000000ull) PASS();
    else FAIL("gettime values wrong");
}

static void t_timerfd_settime_bad_fd(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    struct linux_itimerspec it = { .it_value_sec = 1 };
    int64_t r = linux_timerfd_settime(99, 0, &it, NULL);
    TEST("timerfd_settime: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_timerfd_disarm(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC, 0);
    struct linux_itimerspec arm = { .it_value_sec = 1 };
    linux_timerfd_settime(fd, 0, &arm, NULL);

    /* Disarm by passing all zeros. */
    struct linux_itimerspec zero = {0};
    linux_timerfd_settime(fd, 0, &zero, NULL);

    g_test_now_ns = 5000000000ull;
    uint64_t exp;
    int64_t r = linux_timerfd_read(fd, &exp, sizeof(exp));
    TEST("timerfd_settime zero value: disarms timer (read EAGAIN later)");
    if (r == -LINUX_EAGAIN) PASS();
    else FAIL("disarm did not stick");
}

static void t_poll_timerfd_expiry_and_disarm(void) {
    linux_eventfd_reset_for_tests();
    install_timerfd_clock();
    g_test_now_ns = 0;
    int fd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC,
                                       LINUX_TFD_NONBLOCK);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(fd, 0, &it, NULL);
    uint32_t r0 = linux_eventfd_family_poll_events(fd);
    g_test_now_ns = 1000000000ull;
    uint32_t r1 = linux_eventfd_family_poll_events(fd);
    uint64_t expirations = 0;
    int64_t rr = linux_timerfd_read(fd, &expirations, sizeof(expirations));
    uint32_t r2 = linux_eventfd_family_poll_events(fd);
    TEST("timerfd poll: expiry sets EPOLLIN and one-shot read clears it");
    if (r0 == 0 && r1 == LINUX_EPOLLIN &&
        rr == 8 && expirations == 1 && r2 == 0) PASS();
    else FAIL("timerfd poll readiness wrong");
}

int test_linux_eventfd_run(void) {
    printf("[test_linux_eventfd]\n");
    tests_run = tests_passed = 0;

    t_eventfd2_basic();
    t_eventfd2_unknown_flag();
    t_eventfd2_known_flags();
    t_eventfd2_table_full();
    t_eventfd_install_null_clears_alloc_fd();
    t_eventfd_reset_clears_alloc_fd();

    t_read_basic_reset();
    t_read_after_reset_eagain();
    t_read_semaphore_mode();
    t_read_short_buf();
    t_read_null_buf();
    t_read_bad_fd();

    t_write_basic_increments();
    t_write_sentinel_einval();
    t_write_short_buf();
    t_write_overflow_eagain();
    t_write_bad_fd();
    t_poll_eventfd_read_write_readiness();
    t_poll_eventfd_saturated_not_writable();
    t_poll_eventfd_semaphore_drains_readiness();

    t_signalfd4_unknown_flag();
    t_signalfd4_null_mask_efault();
    t_signalfd4_bad_sigset_size();
    t_signalfd4_create_fd();
    t_signalfd4_update_existing_fd();
    t_signalfd4_eventfd_update_einval();
    t_signalfd_read_no_pending_eagain();
    t_signalfd_read_short_einval();
    t_signalfd4_table_full();
    t_poll_signalfd_no_readiness();
    t_timerfd_create_bad_clock();
    t_timerfd_create_bad_flag();

    t_timerfd_create_known();
    t_timerfd_settime_oneshot();
    t_timerfd_read_before_expiry();
    t_timerfd_read_after_expiry();
    t_timerfd_periodic();
    t_timerfd_gettime();
    t_timerfd_settime_bad_fd();
    t_timerfd_disarm();
    t_poll_timerfd_expiry_and_disarm();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
