#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

struct epoll_entry {
    int      fd;
    uint32_t events;
    uint64_t data;
};

struct epoll_instance {
    int                in_use;
    uint32_t           flags;
    size_t             count;
    struct epoll_entry items[LINUX_EPOLL_MAX_PER_INSTANCE];
};

static struct epoll_instance g_epoll[LINUX_EPOLL_MAX_INSTANCES];
static struct linux_epoll_ops g_ops;

/* Cap on epoll_wait poll iterations when timeout_ms < 0 to
 * prevent kernel-mode spins. Each iteration calls yield. */
#define EPOLL_WAIT_ITER_CAP 1024u

void linux_epoll_install_ops(const struct linux_epoll_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_epoll_ops){0};
}

void linux_epoll_reset_for_tests(void) {
    for (size_t i = 0; i < LINUX_EPOLL_MAX_INSTANCES; i++) {
        g_epoll[i].in_use = 0;
        g_epoll[i].count  = 0;
        g_epoll[i].flags  = 0;
    }
    g_ops = (struct linux_epoll_ops){0};
}

static int alloc_epoll_slot(uint32_t flags) {
    for (size_t i = 0; i < LINUX_EPOLL_MAX_INSTANCES; i++) {
        if (!g_epoll[i].in_use) {
            g_epoll[i].in_use = 1;
            g_epoll[i].count  = 0;
            g_epoll[i].flags  = flags;
            return (int)i;
        }
    }
    return -1;
}

static int epfd_to_slot(int epfd) {
    int slot = epfd - LINUX_EPOLL_FD_BASE;
    if (slot < 0 || slot >= LINUX_EPOLL_MAX_INSTANCES) return -1;
    if (!g_epoll[slot].in_use) return -1;
    return slot;
}

static struct epoll_entry *find_entry(struct epoll_instance *e, int fd) {
    for (size_t i = 0; i < e->count; i++) {
        if (e->items[i].fd == fd) return &e->items[i];
    }
    return NULL;
}

/* ---------- epoll_create1 ---------- */

int64_t linux_epoll_create1(uint32_t flags) {
    if (flags & ~LINUX_EPOLL_CREATE1_KNOWN_FLAGS) return -LINUX_EINVAL;
    int slot = alloc_epoll_slot(flags);
    if (slot < 0) return -LINUX_EMFILE;
    return (int64_t)(LINUX_EPOLL_FD_BASE + slot);
}

/* ---------- epoll_ctl ---------- */

int64_t linux_epoll_ctl(int epfd, int op, int fd,
                        struct linux_epoll_event *event) {
    if (fd < 0) return -LINUX_EBADF;
    /* Linux returns -EINVAL when op is ADD/MOD and event is NULL. */
    if ((op == LINUX_EPOLL_CTL_ADD || op == LINUX_EPOLL_CTL_MOD) && !event) {
        return -LINUX_EFAULT;
    }
    int slot = epfd_to_slot(epfd);
    if (slot < 0) return -LINUX_EBADF;
    struct epoll_instance *e = &g_epoll[slot];

    /* Reject events with unknown bits to keep the contract tight. */
    if (event && (event->events & ~LINUX_EPOLL_KNOWN_EVENTS) != 0) {
        return -LINUX_EINVAL;
    }

    struct epoll_entry *existing = find_entry(e, fd);
    switch (op) {
        case LINUX_EPOLL_CTL_ADD:
            if (existing) return -LINUX_EEXIST;
            if (e->count >= LINUX_EPOLL_MAX_PER_INSTANCE) return -LINUX_ENOMEM;
            e->items[e->count].fd     = fd;
            e->items[e->count].events = event->events;
            e->items[e->count].data   = event->data;
            e->count++;
            return 0;

        case LINUX_EPOLL_CTL_MOD:
            if (!existing) return -LINUX_ENOENT;
            existing->events = event->events;
            existing->data   = event->data;
            return 0;

        case LINUX_EPOLL_CTL_DEL:
            if (!existing) return -LINUX_ENOENT;
            /* Remove by swap-with-last. */
            *existing = e->items[e->count - 1];
            e->count--;
            return 0;

        default:
            return -LINUX_EINVAL;
    }
}

/* ---------- epoll_wait / pwait ---------- */

static int64_t do_epoll_wait(int epfd,
                             struct linux_epoll_event *events_out,
                             int maxevents, int timeout_ms) {
    if (maxevents <= 0) return -LINUX_EINVAL;
    if (!events_out) return -LINUX_EFAULT;

    int slot = epfd_to_slot(epfd);
    if (slot < 0) return -LINUX_EBADF;
    struct epoll_instance *e = &g_epoll[slot];

    /* Single non-blocking pass: collect ready events. */
    uint32_t iterations = (timeout_ms == 0) ? 1 : EPOLL_WAIT_ITER_CAP;
    for (uint32_t it = 0; it < iterations; it++) {
        int n_ready = 0;
        for (size_t i = 0; i < e->count && n_ready < maxevents; i++) {
            uint32_t got = 0;
            if (g_ops.fd_ready) got = g_ops.fd_ready(e->items[i].fd);
            uint32_t interest = e->items[i].events;
            uint32_t emit = interest
                ? ((got & interest) | (got & (LINUX_EPOLLERR | LINUX_EPOLLHUP)))
                : 0;
            if (emit) {
                events_out[n_ready].events = emit;
                events_out[n_ready].data   = e->items[i].data;
                n_ready++;
                /* EPOLLONESHOT: clear the event mask after firing. */
                if (e->items[i].events & LINUX_EPOLLONESHOT) {
                    e->items[i].events = 0;
                }
            }
        }
        if (n_ready > 0 || timeout_ms == 0) return (int64_t)n_ready;
        if (g_ops.yield) g_ops.yield();
    }
    /* Timed out (or hit the iteration cap). */
    return 0;
}

int64_t linux_epoll_wait(int epfd, struct linux_epoll_event *events_out,
                         int maxevents, int timeout_ms) {
    return do_epoll_wait(epfd, events_out, maxevents, timeout_ms);
}

int64_t linux_epoll_pwait(int epfd, struct linux_epoll_event *events_out,
                          int maxevents, int timeout_ms,
                          uint64_t sigmask_ptr, size_t sigsetsize) {
    /* Linux validates sigsetsize == 8 (sizeof sigset_t). Reject others. */
    if (sigmask_ptr != 0 && sigsetsize != 8) return -LINUX_EINVAL;
    /* sigmask is accepted but not applied yet (S1.12 stores only). */
    return do_epoll_wait(epfd, events_out, maxevents, timeout_ms);
}

int64_t linux_epoll_close(int epfd) {
    int slot = epfd_to_slot(epfd);
    if (slot < 0) return -LINUX_EBADF;
    g_epoll[slot].in_use = 0;
    g_epoll[slot].count  = 0;
    g_epoll[slot].flags  = 0;
    return 0;
}

int64_t linux_epoll_read(int epfd, void *buf, size_t len) {
    if (epfd_to_slot(epfd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_epoll_write(int epfd, const void *buf, size_t len) {
    if (epfd_to_slot(epfd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_epoll_lseek(int epfd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (epfd_to_slot(epfd) < 0) return -LINUX_EBADF;
    return -LINUX_ESPIPE;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_epoll_create1(const struct linux_syscall_args *a) {
    return linux_epoll_create1((uint32_t)a->a0);
}

static int64_t sys_epoll_ctl(const struct linux_syscall_args *a) {
    return linux_epoll_ctl((int)a->a0, (int)a->a1, (int)a->a2,
                           (struct linux_epoll_event *)(uintptr_t)a->a3);
}

static int64_t sys_epoll_wait(const struct linux_syscall_args *a) {
    return linux_epoll_wait((int)a->a0,
                            (struct linux_epoll_event *)(uintptr_t)a->a1,
                            (int)a->a2, (int)a->a3);
}

static int64_t sys_epoll_pwait(const struct linux_syscall_args *a) {
    return linux_epoll_pwait((int)a->a0,
                             (struct linux_epoll_event *)(uintptr_t)a->a1,
                             (int)a->a2, (int)a->a3,
                             a->a4, (size_t)a->a5);
}

void linux_epoll_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_epoll_create1, sys_epoll_create1);
    (void)linux_syscall_register(LINUX_NR_epoll_ctl,     sys_epoll_ctl);
    (void)linux_syscall_register(LINUX_NR_epoll_wait,    sys_epoll_wait);
    (void)linux_syscall_register(LINUX_NR_epoll_pwait,   sys_epoll_pwait);
}
