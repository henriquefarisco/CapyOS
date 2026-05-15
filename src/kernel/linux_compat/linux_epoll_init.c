#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/pipe.h"

/* Boot wiring for `linux_epoll`. Excluded from host tests.
 *
 * Marco M1: eventfd/timerfd readiness is provided by linux_eventfd.
 * Pipe readiness is provided by the kernel pipe table. Other fd classes
 * still surface no events until they expose a poll oracle. yield is wired
 * to `task_yield` so the loop cooperates with the scheduler.
 */

#if !defined(UNIT_TEST)

#include "kernel/task.h"

#include <stdint.h>

static uint32_t wrap_fd_ready(int fd) {
    uint32_t events = linux_eventfd_family_poll_events(fd);
    if (events) return events;
    uint32_t pipe_events = pipe_poll_events(fd);
    if (pipe_events & PIPE_POLLIN) events |= LINUX_EPOLLIN;
    if (pipe_events & PIPE_POLLOUT) events |= LINUX_EPOLLOUT;
    if (pipe_events & PIPE_POLLERR) events |= LINUX_EPOLLERR;
    if (pipe_events & PIPE_POLLHUP) events |= LINUX_EPOLLHUP;
    return events;
}

static void wrap_yield(void) {
    task_yield();
}

void linux_epoll_init_boot(void) {
    static const struct linux_epoll_ops ops = {
        .fd_ready = wrap_fd_ready,
        .yield    = wrap_yield,
    };
    linux_epoll_install_ops(&ops);
}

#endif /* !UNIT_TEST */
