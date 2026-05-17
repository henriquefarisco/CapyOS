/* Host tests for linux_epoll (S1.6). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/pipe.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake fd_ready / yield. */
static uint32_t g_ready_for_fd[16];
static int g_yield_calls;
static uint64_t g_test_now_ns;

static uint32_t fake_fd_ready(int fd) {
    if (fd >= 0 && fd < 16) return g_ready_for_fd[fd];
    return 0;
}
static void fake_yield(void) { g_yield_calls++; }

static void install_fake(void) {
    linux_epoll_reset_for_tests();
    g_yield_calls = 0;
    for (int i = 0; i < 16; i++) g_ready_for_fd[i] = 0;
    static const struct linux_epoll_ops ops = {
        .fd_ready = fake_fd_ready,
        .yield    = fake_yield,
    };
    linux_epoll_install_ops(&ops);
}

static uint64_t fake_timer_now_ns(void) {
    return g_test_now_ns;
}

static void install_eventfd_ready(void) {
    linux_epoll_reset_for_tests();
    linux_eventfd_reset_for_tests();
    g_yield_calls = 0;
    g_test_now_ns = 0;
    linux_eventfd_install_now_ns(fake_timer_now_ns);
    static const struct linux_epoll_ops ops = {
        .fd_ready = linux_eventfd_family_poll_events,
        .yield    = fake_yield,
    };
    linux_epoll_install_ops(&ops);
}

static uint32_t fake_pipe_epoll_ready(int fd) {
    uint32_t pipe_events = pipe_poll_events(fd);
    uint32_t events = 0;
    if (pipe_events & PIPE_POLLIN) events |= LINUX_EPOLLIN;
    if (pipe_events & PIPE_POLLOUT) events |= LINUX_EPOLLOUT;
    if (pipe_events & PIPE_POLLERR) events |= LINUX_EPOLLERR;
    if (pipe_events & PIPE_POLLHUP) events |= LINUX_EPOLLHUP;
    return events;
}

static void install_pipe_ready(void) {
    linux_epoll_reset_for_tests();
    pipe_system_init();
    g_yield_calls = 0;
    static const struct linux_epoll_ops ops = {
        .fd_ready = fake_pipe_epoll_ready,
        .yield    = fake_yield,
    };
    linux_epoll_install_ops(&ops);
}

/* -------- create1 -------- */

static void t_create1_basic(void) {
    install_fake();
    int64_t r = linux_epoll_create1(0);
    TEST("epoll_create1: basic returns fd >= LINUX_EPOLL_FD_BASE");
    if (r >= LINUX_EPOLL_FD_BASE) PASS();
    else FAIL("fd out of range");
}

static void t_create1_unknown_flag(void) {
    install_fake();
    int64_t r = linux_epoll_create1(0x1000u);
    TEST("epoll_create1: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_create1_table_full(void) {
    install_fake();
    for (int i = 0; i < LINUX_EPOLL_MAX_INSTANCES; i++) {
        if (linux_epoll_create1(0) < 0) { FAIL("alloc failed early"); return; }
    }
    int64_t r = linux_epoll_create1(0);
    TEST("epoll_create1: table full -> -EMFILE");
    if (r == -LINUX_EMFILE) PASS();
    else FAIL("EMFILE not surfaced");
}

/* -------- ctl -------- */

static void t_ctl_add_basic(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xAAAA };
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    TEST("epoll_ctl ADD: basic returns 0");
    if (r == 0) PASS();
    else FAIL("ADD failed");
}

static void t_ctl_add_duplicate_eexist(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    TEST("epoll_ctl ADD twice on same fd -> -EEXIST");
    if (r == -LINUX_EEXIST) PASS();
    else FAIL("EEXIST not surfaced");
}

static void t_ctl_mod_basic(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    ev.events = LINUX_EPOLLOUT;
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_MOD, 3, &ev);
    TEST("epoll_ctl MOD: returns 0 on existing fd");
    if (r == 0) PASS();
    else FAIL("MOD failed");
}

static void t_ctl_mod_missing_enoent(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_MOD, 5, &ev);
    TEST("epoll_ctl MOD on unknown fd -> -ENOENT");
    if (r == -LINUX_ENOENT) PASS();
    else FAIL("ENOENT not surfaced");
}

static void t_ctl_del_basic(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_DEL, 3, NULL);
    TEST("epoll_ctl DEL: returns 0; subsequent MOD -> -ENOENT");
    int64_t r2 = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_MOD, 3, &ev);
    if (r == 0 && r2 == -LINUX_ENOENT) PASS();
    else FAIL("DEL did not remove entry");
}

static void t_ctl_unknown_event_bit(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = 0x00010000u, .data = 0 };
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    TEST("epoll_ctl: unknown event bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown event accepted");
}

static void t_ctl_negative_fd(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, -1, &ev);
    TEST("epoll_ctl: negative fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("negative fd accepted");
}

static void t_ctl_bad_epfd(void) {
    install_fake();
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    int64_t r = linux_epoll_ctl(99999, LINUX_EPOLL_CTL_ADD, 3, &ev);
    TEST("epoll_ctl: unknown epfd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

static void t_ctl_add_null_event_efault(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    int64_t r = linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, NULL);
    TEST("epoll_ctl ADD: NULL event -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

/* -------- wait -------- */

static void t_wait_no_events_timeout0(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: nothing ready, timeout=0 -> 0 events, no yield");
    if (r == 0 && g_yield_calls == 0) PASS();
    else FAIL("expected 0/0");
}

static void t_wait_one_ready(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xCAFE };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    g_ready_for_fd[3] = LINUX_EPOLLIN;

    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 100);
    TEST("epoll_wait: 1 fd ready -> 1 event w/ correct data and mask");
    if (r == 1 && out[0].events == LINUX_EPOLLIN && out[0].data == 0xCAFE) PASS();
    else FAIL("event shape wrong");
}

static void t_wait_event_filtered_by_mask(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };  /* only IN */
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    g_ready_for_fd[3] = LINUX_EPOLLOUT;  /* only OUT ready */

    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: ready bits filtered by event mask (no overlap -> 0)");
    if (r == 0) PASS();
    else FAIL("filter not applied");
}

static void t_wait_oneshot_clears(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN | LINUX_EPOLLONESHOT };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    g_ready_for_fd[3] = LINUX_EPOLLIN;

    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait ONESHOT: 1st fires, 2nd returns 0 (mask cleared)");
    if (r1 == 1 && r2 == 0) PASS();
    else FAIL("oneshot did not disarm");
}

static void t_wait_negative_maxevents(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 0, 0);
    TEST("epoll_wait: maxevents <= 0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_wait_null_events(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    int64_t r = linux_epoll_wait(epfd, NULL, 4, 0);
    TEST("epoll_wait: NULL events_out -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS();
    else FAIL("NULL not rejected");
}

static void t_install_null_clears_wait_callbacks(void) {
    install_fake();
    linux_epoll_install_ops(NULL);
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    g_ready_for_fd[3] = LINUX_EPOLLIN;
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 1);
    TEST("epoll install_ops(NULL) clears wait callbacks");
    if (r == 0 && g_yield_calls == 0) PASS();
    else FAIL("stale epoll callback invoked");
}

static void t_reset_clears_wait_callbacks(void) {
    install_fake();
    linux_epoll_reset_for_tests();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, 3, &ev);
    g_ready_for_fd[3] = LINUX_EPOLLIN;
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 1);
    TEST("epoll reset clears installed callbacks");
    if (r == 0 && g_yield_calls == 0) PASS();
    else FAIL("stale epoll callback invoked");
}

static void t_wait_eventfd_readiness(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int efd = (int)linux_eventfd2(0, LINUX_EFD_NONBLOCK);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xEFD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, efd, &ev);
    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    linux_eventfd_write(efd, 1, sizeof(uint64_t));
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: eventfd counter drives EPOLLIN readiness");
    if (r1 == 0 && r2 == 1 && out[0].events == LINUX_EPOLLIN &&
        out[0].data == 0xEFD) PASS();
    else FAIL("eventfd readiness not surfaced");
}

static void t_wait_eventfd_writable_readiness(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int efd = (int)linux_eventfd2(0, LINUX_EFD_NONBLOCK);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLOUT, .data = 0x0FD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, efd, &ev);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: writable eventfd drives EPOLLOUT readiness");
    if (r == 1 && out[0].events == LINUX_EPOLLOUT &&
        out[0].data == 0x0FD) PASS();
    else FAIL("eventfd writable readiness not surfaced");
}

static void t_wait_eventfd_readiness_drains_after_read(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int efd = (int)linux_eventfd2(2, LINUX_EFD_NONBLOCK);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xDFD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, efd, &ev);
    struct linux_epoll_event out[4];
    uint64_t value = 0;
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    int64_t rr = linux_eventfd_read(efd, &value, sizeof(value));
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: eventfd EPOLLIN clears after draining read");
    if (r1 == 1 && rr == 8 && value == 2 && r2 == 0) PASS();
    else FAIL("eventfd read readiness did not drain");
}

static void t_wait_timerfd_readiness(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int tfd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC,
                                        LINUX_TFD_NONBLOCK);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(tfd, 0, &it, NULL);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0x7FD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, tfd, &ev);
    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    g_test_now_ns = 1000000000ull;
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: expired timerfd drives EPOLLIN readiness");
    if (r1 == 0 && r2 == 1 && out[0].events == LINUX_EPOLLIN &&
        out[0].data == 0x7FD) PASS();
    else FAIL("timerfd readiness not surfaced");
}

static void t_wait_timerfd_oneshot_drains_after_read(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int tfd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC,
                                        LINUX_TFD_NONBLOCK);
    struct linux_itimerspec it = { .it_value_sec = 1 };
    linux_timerfd_settime(tfd, 0, &it, NULL);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0x1FD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, tfd, &ev);
    struct linux_epoll_event out[4];
    uint64_t expirations = 0;
    g_test_now_ns = 1000000000ull;
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    int64_t rr = linux_timerfd_read(tfd, &expirations, sizeof(expirations));
    g_test_now_ns = 2000000000ull;
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: one-shot timerfd clears EPOLLIN after read");
    if (r1 == 1 && rr == 8 && expirations == 1 && r2 == 0) PASS();
    else FAIL("one-shot timerfd readiness did not clear");
}

static void t_wait_timerfd_periodic_rearms_after_read(void) {
    install_eventfd_ready();
    int epfd = (int)linux_epoll_create1(0);
    int tfd = (int)linux_timerfd_create(LINUX_CLOCK_MONOTONIC,
                                        LINUX_TFD_NONBLOCK);
    struct linux_itimerspec it = {
        .it_value_sec = 1,
        .it_interval_sec = 1,
    };
    linux_timerfd_settime(tfd, 0, &it, NULL);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0x2FD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, tfd, &ev);
    struct linux_epoll_event out[4];
    uint64_t expirations = 0;
    g_test_now_ns = 1000000000ull;
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    int64_t rr = linux_timerfd_read(tfd, &expirations, sizeof(expirations));
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    g_test_now_ns = 2000000000ull;
    int64_t r3 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: periodic timerfd re-arms next EPOLLIN after read");
    if (r1 == 1 && rr == 8 && expirations == 1 && r2 == 0 &&
        r3 == 1 && out[0].data == 0x2FD) PASS();
    else FAIL("periodic timerfd readiness did not rearm");
}

static void t_wait_pipe_readiness(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xB1FE };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[0], &ev);
    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    pipe_write(fds[0], "p", 1);
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe read end drives EPOLLIN readiness");
    if (r1 == 0 && r2 == 1 && out[0].events == LINUX_EPOLLIN &&
        out[0].data == 0xB1FE) PASS();
    else FAIL("pipe readiness not surfaced");
}

static void t_wait_pipe_write_readiness(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLOUT, .data = 0xB007 };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[1], &ev);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe write end drives EPOLLOUT readiness");
    if (r == 1 && out[0].events == LINUX_EPOLLOUT &&
        out[0].data == 0xB007) PASS();
    else FAIL("pipe writable readiness not surfaced");
}

static void t_wait_pipe_full_write_not_ready(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    static char buf[PIPE_BUF_SIZE];
    for (uint32_t i = 0; i < PIPE_BUF_SIZE; i++) buf[i] = 'q';
    int wr = pipe_write(fds[0], buf, PIPE_BUF_SIZE);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLOUT };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[1], &ev);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: full pipe write end does not drive EPOLLOUT");
    if (wr == (int)PIPE_BUF_SIZE && r == 0) PASS();
    else FAIL("full pipe unexpectedly writable");
}

static void t_wait_pipe_read_hup_unmasked(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLIN, .data = 0xBADD };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[0], &ev);
    pipe_close_write(fds[0]);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe read end surfaces EPOLLHUP even if unmasked");
    if (r == 1 && out[0].events == LINUX_EPOLLHUP &&
        out[0].data == 0xBADD) PASS();
    else FAIL("pipe HUP readiness not surfaced");
}

static void t_wait_pipe_write_err_unmasked(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    struct linux_epoll_event ev = { .events = LINUX_EPOLLOUT, .data = 0xE220 };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[1], &ev);
    pipe_close_read(fds[0]);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe write end surfaces EPOLLERR even if unmasked");
    if (r == 1 && out[0].events == LINUX_EPOLLERR &&
        out[0].data == 0xE220) PASS();
    else FAIL("pipe ERR readiness not surfaced");
}

static void t_wait_pipe_hup_oneshot_stays_disarmed(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    pipe_write(fds[0], "h", 1);
    struct linux_epoll_event ev = {
        .events = LINUX_EPOLLIN | LINUX_EPOLLONESHOT,
        .data = 0x0FAD,
    };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[0], &ev);
    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    pipe_close_write(fds[0]);
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe EPOLLHUP does not revive ONESHOT entry");
    if (r1 == 1 && out[0].events == LINUX_EPOLLIN &&
        out[0].data == 0x0FAD && r2 == 0) PASS();
    else FAIL("pipe HUP revived oneshot entry");
}

static void t_wait_pipe_err_oneshot_stays_disarmed(void) {
    install_pipe_ready();
    int epfd = (int)linux_epoll_create1(0);
    int fds[2];
    pipe_create(fds);
    struct linux_epoll_event ev = {
        .events = LINUX_EPOLLOUT | LINUX_EPOLLONESHOT,
        .data = 0xE0AD,
    };
    linux_epoll_ctl(epfd, LINUX_EPOLL_CTL_ADD, fds[1], &ev);
    struct linux_epoll_event out[4];
    int64_t r1 = linux_epoll_wait(epfd, out, 4, 0);
    pipe_close_read(fds[0]);
    int64_t r2 = linux_epoll_wait(epfd, out, 4, 0);
    TEST("epoll_wait: pipe EPOLLERR does not revive ONESHOT entry");
    if (r1 == 1 && out[0].events == LINUX_EPOLLOUT &&
        out[0].data == 0xE0AD && r2 == 0) PASS();
    else FAIL("pipe ERR revived oneshot entry");
}

static void t_pwait_bad_sigsetsize(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    struct linux_epoll_event out[4];
    int64_t r = linux_epoll_pwait(epfd, out, 4, 0, 0xDEAD, 16);
    TEST("epoll_pwait: sigmask non-null + sigsetsize != 8 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("sigsetsize not validated");
}

static void t_close_releases_slot(void) {
    install_fake();
    int64_t epfd = linux_epoll_create1(0);
    int64_t c = linux_epoll_close((int)epfd);
    int64_t again = linux_epoll_create1(0);
    TEST("epoll close releases fd slot for reuse");
    if (c == 0 && again == epfd) PASS();
    else FAIL("slot not reused");
}

static void t_read_einval(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    uint8_t buf[8];
    int64_t r = linux_epoll_read(epfd, buf, sizeof(buf));
    TEST("epoll read: not supported -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_write_einval(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    int64_t r = linux_epoll_write(epfd, "x", 1);
    TEST("epoll write: not supported -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("EINVAL not surfaced");
}

static void t_lseek_espipe(void) {
    install_fake();
    int epfd = (int)linux_epoll_create1(0);
    int64_t r = linux_epoll_lseek(epfd, 0, 0);
    TEST("epoll lseek: stream fd -> -ESPIPE");
    if (r == -LINUX_ESPIPE) PASS();
    else FAIL("ESPIPE not surfaced");
}

static void t_read_bad_fd(void) {
    install_fake();
    uint8_t buf[8];
    int64_t r = linux_epoll_read(LINUX_EPOLL_FD_BASE, buf, sizeof(buf));
    TEST("epoll read: unknown fd -> -EBADF");
    if (r == -LINUX_EBADF) PASS();
    else FAIL("EBADF not surfaced");
}

int test_linux_epoll_run(void) {
    printf("[test_linux_epoll]\n");
    tests_run = tests_passed = 0;

    t_create1_basic();
    t_create1_unknown_flag();
    t_create1_table_full();

    t_ctl_add_basic();
    t_ctl_add_duplicate_eexist();
    t_ctl_mod_basic();
    t_ctl_mod_missing_enoent();
    t_ctl_del_basic();
    t_ctl_unknown_event_bit();
    t_ctl_negative_fd();
    t_ctl_bad_epfd();
    t_ctl_add_null_event_efault();

    t_wait_no_events_timeout0();
    t_wait_one_ready();
    t_wait_event_filtered_by_mask();
    t_wait_oneshot_clears();
    t_wait_negative_maxevents();
    t_wait_null_events();
    t_install_null_clears_wait_callbacks();
    t_reset_clears_wait_callbacks();
    t_wait_eventfd_readiness();
    t_wait_eventfd_writable_readiness();
    t_wait_eventfd_readiness_drains_after_read();
    t_wait_timerfd_readiness();
    t_wait_timerfd_oneshot_drains_after_read();
    t_wait_timerfd_periodic_rearms_after_read();
    t_wait_pipe_readiness();
    t_wait_pipe_write_readiness();
    t_wait_pipe_full_write_not_ready();
    t_wait_pipe_read_hup_unmasked();
    t_wait_pipe_write_err_unmasked();
    t_wait_pipe_hup_oneshot_stays_disarmed();
    t_wait_pipe_err_oneshot_stays_disarmed();
    t_pwait_bad_sigsetsize();
    t_close_releases_slot();
    t_read_einval();
    t_write_einval();
    t_lseek_espipe();
    t_read_bad_fd();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
