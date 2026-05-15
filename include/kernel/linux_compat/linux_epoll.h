#ifndef KERNEL_LINUX_COMPAT_LINUX_EPOLL_H
#define KERNEL_LINUX_COMPAT_LINUX_EPOLL_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `epoll_create1`/`epoll_ctl`/`epoll_wait`/`epoll_pwait`
 * shim (S1.6).
 *
 * libevent on Linux uses epoll as the default backend; Chromium's
 * IPC layer also depends on it. The shim provides:
 *
 *   epoll_create1(flags)              -- create epoll fd
 *   epoll_ctl(epfd, op, fd, event)    -- add/mod/del fd of interest
 *   epoll_wait(epfd, events, n, timeout_ms)  -- wait for events
 *   epoll_pwait(epfd, events, n, timeout_ms, sigmask, sigsize)
 *
 * Marco M1 strategy: in-memory epoll fd table with per-epfd
 * interest list. epoll_wait does a non-blocking poll over the
 * list using injected `fd_ready` callback; if no event is ready
 * and timeout_ms > 0, we yield via `task_yield` and retry up to
 * a safety cap (no real timer integration yet).
 *
 * Limitations vs Linux mainline:
 *   - epoll_pwait sigmask is validated but not applied (signal
 *     mask infra in S1.12 is just stored, no delivery yet).
 *   - timeout_ms < 0 ("infinite") falls back to a hardcoded
 *     iteration cap to prevent kernel-mode spins.
 *   - Edge-triggered (EPOLLET) is documented as accepted but
 *     functionally identical to level-triggered until we have
 *     real wakeup chains. Chromium IPC and libevent default to
 *     LT so this is benign for Marco M1.
 */

/* Linux flag bits (uapi/linux/eventpoll.h). */
#define LINUX_EPOLL_CLOEXEC 0x080000u  /* same bit as O_CLOEXEC */

#define LINUX_EPOLL_CREATE1_KNOWN_FLAGS LINUX_EPOLL_CLOEXEC

/* epoll_ctl ops. */
#define LINUX_EPOLL_CTL_ADD 1
#define LINUX_EPOLL_CTL_DEL 2
#define LINUX_EPOLL_CTL_MOD 3

/* Event bits userland passes. We track the canonical subset. */
#define LINUX_EPOLLIN      0x00000001u
#define LINUX_EPOLLPRI     0x00000002u
#define LINUX_EPOLLOUT     0x00000004u
#define LINUX_EPOLLERR     0x00000008u
#define LINUX_EPOLLHUP     0x00000010u
#define LINUX_EPOLLRDNORM  0x00000040u
#define LINUX_EPOLLWRNORM  0x00000100u
#define LINUX_EPOLLMSG     0x00000400u
#define LINUX_EPOLLRDHUP   0x00002000u
#define LINUX_EPOLLEXCLUSIVE 0x10000000u
#define LINUX_EPOLLWAKEUP    0x20000000u
#define LINUX_EPOLLONESHOT 0x40000000u
#define LINUX_EPOLLET      0x80000000u

#define LINUX_EPOLL_KNOWN_EVENTS \
    (LINUX_EPOLLIN | LINUX_EPOLLPRI | LINUX_EPOLLOUT | LINUX_EPOLLERR | \
     LINUX_EPOLLHUP | LINUX_EPOLLRDNORM | LINUX_EPOLLWRNORM | \
     LINUX_EPOLLMSG | LINUX_EPOLLRDHUP | LINUX_EPOLLEXCLUSIVE | \
     LINUX_EPOLLWAKEUP | LINUX_EPOLLONESHOT | LINUX_EPOLLET)

/* Linux struct epoll_event (uapi/linux/eventpoll.h, packed=12 bytes). */
struct linux_epoll_event {
    uint32_t events;
    uint64_t data;  /* opaque, union of u64/ptr/fd/u32 */
} __attribute__((packed));

/* Internal slot table sizing. */
#define LINUX_EPOLL_MAX_INSTANCES 16
#define LINUX_EPOLL_MAX_PER_INSTANCE 64
/* fd encoding: epoll fds occupy 0x6000..0x6000+MAX_INSTANCES so they
 * do not collide with eventfd (0x4000 range) or pipe ids (<256). */
#define LINUX_EPOLL_FD_BASE 0x6000

/* Ops. The only callback we need is "is fd ready right now",
 * for the poll loop. NULL means "no fd is ever ready" (Marco
 * M1 default until libevent fully integrates). */
struct linux_epoll_ops {
    /* Returns the bitmask of currently-ready events on `fd`, or
     * 0 if none. Called repeatedly during epoll_wait. */
    uint32_t (*fd_ready)(int fd);
    /* Yield to the scheduler between poll passes. */
    void (*yield)(void);
};

void linux_epoll_install_ops(const struct linux_epoll_ops *ops);
void linux_epoll_reset_for_tests(void);

int64_t linux_epoll_create1(uint32_t flags);
int64_t linux_epoll_ctl(int epfd, int op, int fd,
                        struct linux_epoll_event *event);
int64_t linux_epoll_wait(int epfd, struct linux_epoll_event *events_out,
                         int maxevents, int timeout_ms);
int64_t linux_epoll_pwait(int epfd, struct linux_epoll_event *events_out,
                          int maxevents, int timeout_ms,
                          uint64_t sigmask_ptr, size_t sigsetsize);
int64_t linux_epoll_close(int epfd);
int64_t linux_epoll_read(int epfd, void *buf, size_t len);
int64_t linux_epoll_write(int epfd, const void *buf, size_t len);
int64_t linux_epoll_lseek(int epfd, int64_t offset, int whence);

void linux_epoll_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_EPOLL_H */
