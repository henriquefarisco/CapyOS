#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_signal.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

/* Per-eventfd entry. */
struct eventfd_slot {
    int      in_use;
    uint64_t counter;
    uint32_t flags;
};

static struct eventfd_slot     g_eventfds[LINUX_EVENTFD_MAX];
static struct linux_eventfd_ops g_ops;

struct signalfd_slot {
    int      in_use;
    uint64_t mask;
    uint32_t flags;
};

static struct signalfd_slot g_signalfds[LINUX_SIGNALFD_MAX];

void linux_eventfd_install_ops(const struct linux_eventfd_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_eventfd_ops){0};
}

/* Forward decl: implementation lives next to the timerfd state so
 * the reset function can be called from the public reset_for_tests
 * defined here. */
static void reset_timerfds(void);
static int tfd_slot(int fd);

void linux_eventfd_reset_for_tests(void) {
    for (int i = 0; i < LINUX_EVENTFD_MAX; i++) {
        g_eventfds[i].in_use  = 0;
        g_eventfds[i].counter = 0;
        g_eventfds[i].flags   = 0;
    }
    for (int i = 0; i < LINUX_SIGNALFD_MAX; i++) {
        g_signalfds[i].in_use = 0;
        g_signalfds[i].mask   = 0;
        g_signalfds[i].flags  = 0;
    }
    reset_timerfds();
    g_ops = (struct linux_eventfd_ops){0};
}

/* Resolve a Linux fd back to a slot index (0..MAX-1).
 * Returns -1 if the fd is not a known eventfd. */
static int fd_to_slot(int fd) {
    int slot = fd - LINUX_EVENTFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_EVENTFD_MAX) return -1;
    if (!g_eventfds[slot].in_use) return -1;
    return slot;
}

/* Allocate an unused slot or return -1. */
static int alloc_slot(uint64_t initval, uint32_t flags) {
    for (int i = 0; i < LINUX_EVENTFD_MAX; i++) {
        if (!g_eventfds[i].in_use) {
            g_eventfds[i].in_use  = 1;
            g_eventfds[i].counter = initval;
            g_eventfds[i].flags   = flags;
            return i;
        }
    }
    return -1;
}

/* ------------ eventfd2 ------------ */

int64_t linux_eventfd2(uint64_t initval, uint32_t flags) {
    if (flags & ~LINUX_EFD_KNOWN_FLAGS) return -LINUX_EINVAL;

    int slot = alloc_slot(initval, flags);
    if (slot < 0) return -LINUX_EMFILE;

    int fd = LINUX_EVENTFD_FD_BASE + slot;
    if (g_ops.alloc_fd) {
        int rc = g_ops.alloc_fd(slot, flags);
        if (rc >= 0) fd = rc;
    }
    return (int64_t)fd;
}

int64_t linux_eventfd_read(int fd, uint64_t *out, size_t len) {
    if (len < sizeof(uint64_t)) return -LINUX_EINVAL;
    if (!out) return -LINUX_EFAULT;

    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;

    struct eventfd_slot *e = &g_eventfds[slot];

    if (e->counter == 0) {
        /* NONBLOCK: return EAGAIN. Without it Linux blocks; we
         * cannot block in pure logic, so always EAGAIN for now.
         * The kernel side will integrate with task_block when
         * blocking-eventfd readers actually exist (libevent uses
         * eventfd in NONBLOCK by default). */
        return -LINUX_EAGAIN;
    }

    if (e->flags & LINUX_EFD_SEMAPHORE) {
        *out = 1;
        e->counter--;
    } else {
        *out = e->counter;
        e->counter = 0;
    }
    return (int64_t)sizeof(uint64_t);
}

int64_t linux_eventfd_write(int fd, uint64_t value, size_t len) {
    if (len < sizeof(uint64_t)) return -LINUX_EINVAL;
    /* Linux rejects 0xFFFFFFFFFFFFFFFF as a sentinel for ULLONG_MAX. */
    if (value == (uint64_t)-1) return -LINUX_EINVAL;

    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;

    struct eventfd_slot *e = &g_eventfds[slot];

    /* Linux refuses if counter would overflow past 0xFFFFFFFFFFFFFFFE. */
    if (e->counter > (uint64_t)-2 - value) {
        /* NONBLOCK semantics: -EAGAIN. Same caveat as read above. */
        return -LINUX_EAGAIN;
    }
    e->counter += value;
    return (int64_t)sizeof(uint64_t);
}

/* ------------ signalfd4 ------------ */

static uint64_t signalfd_sanitise_mask(uint64_t mask) {
    mask &= ~((uint64_t)1 << (LINUX_SIGKILL - 1));
    mask &= ~((uint64_t)1 << (LINUX_SIGSTOP - 1));
    return mask;
}

static int signalfd_slot(int fd) {
    int slot = fd - LINUX_SIGNALFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_SIGNALFD_MAX) return -1;
    if (!g_signalfds[slot].in_use) return -1;
    return slot;
}

static int alloc_signalfd(uint64_t mask, uint32_t flags) {
    for (int i = 0; i < LINUX_SIGNALFD_MAX; i++) {
        if (!g_signalfds[i].in_use) {
            g_signalfds[i].in_use = 1;
            g_signalfds[i].mask   = mask;
            g_signalfds[i].flags  = flags;
            return i;
        }
    }
    return -1;
}

int64_t linux_signalfd4(int fd, uint64_t mask_ptr, size_t sizemask,
                        uint32_t flags) {
    if (flags & ~LINUX_SFD_KNOWN_FLAGS) return -LINUX_EINVAL;
    if (sizemask != 8) return -LINUX_EINVAL;
    if (mask_ptr == 0) return -LINUX_EFAULT;

    uint64_t mask = *(const uint64_t *)(uintptr_t)mask_ptr;
    mask = signalfd_sanitise_mask(mask);

    if (fd == -1) {
        int slot = alloc_signalfd(mask, flags);
        if (slot < 0) return -LINUX_EMFILE;
        return LINUX_SIGNALFD_FD_BASE + slot;
    }
    if (fd < -1) return -LINUX_EBADF;

    int slot = signalfd_slot(fd);
    if (slot >= 0) {
        g_signalfds[slot].mask  = mask;
        g_signalfds[slot].flags = flags;
        return fd;
    }
    if (fd_to_slot(fd) >= 0 || tfd_slot(fd) >= 0) {
        return -LINUX_EINVAL;
    }
    return -LINUX_EBADF;
}

int64_t linux_signalfd_read(int fd, void *out, size_t len) {
    if (len < LINUX_SIGNALFD_SIGINFO_SIZE) return -LINUX_EINVAL;
    if (!out) return -LINUX_EFAULT;
    if (signalfd_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_EAGAIN;
}

/* ------------ timerfd functional ------------ */

struct timerfd_slot {
    int      in_use;
    int      clockid;
    uint32_t flags;
    /* Stored interval/initial expiration in nanoseconds. */
    uint64_t interval_ns;
    uint64_t expires_at_ns;     /* absolute time of next expiration */
    uint64_t last_read_now_ns;  /* clock at last read (for gettime relative) */
};

static struct timerfd_slot         g_timerfds[LINUX_TIMERFD_MAX_INSTANCES];
static linux_eventfd_now_ns_fn     g_now_ns;

void linux_eventfd_install_now_ns(linux_eventfd_now_ns_fn fn) {
    g_now_ns = fn;
}

static void reset_timerfds(void) {
    for (int i = 0; i < LINUX_TIMERFD_MAX_INSTANCES; i++) {
        g_timerfds[i].in_use        = 0;
        g_timerfds[i].interval_ns   = 0;
        g_timerfds[i].expires_at_ns = 0;
    }
    g_now_ns = NULL;
}

static uint64_t now_ns(void) {
    return g_now_ns ? g_now_ns() : 0;
}

static int tfd_slot(int fd) {
    int slot = fd - LINUX_TIMERFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_TIMERFD_MAX_INSTANCES) return -1;
    if (!g_timerfds[slot].in_use) return -1;
    return slot;
}

uint32_t linux_eventfd_family_poll_events(int fd) {
    int slot = fd_to_slot(fd);
    if (slot >= 0) {
        struct eventfd_slot *e = &g_eventfds[slot];
        uint32_t events = 0;
        if (e->counter > 0) events |= LINUX_EPOLLIN;
        if (e->counter <= (uint64_t)-3) events |= LINUX_EPOLLOUT;
        return events;
    }
    if (signalfd_slot(fd) >= 0) return 0;
    slot = tfd_slot(fd);
    if (slot >= 0) {
        struct timerfd_slot *t = &g_timerfds[slot];
        if (t->expires_at_ns != 0 && now_ns() >= t->expires_at_ns) {
            return LINUX_EPOLLIN;
        }
    }
    return 0;
}

int64_t linux_eventfd_family_close(int fd) {
    int slot = fd_to_slot(fd);
    if (slot >= 0) {
        g_eventfds[slot].in_use  = 0;
        g_eventfds[slot].counter = 0;
        g_eventfds[slot].flags   = 0;
        return 0;
    }
    slot = signalfd_slot(fd);
    if (slot >= 0) {
        g_signalfds[slot].in_use = 0;
        g_signalfds[slot].mask   = 0;
        g_signalfds[slot].flags  = 0;
        return 0;
    }
    slot = tfd_slot(fd);
    if (slot >= 0) {
        g_timerfds[slot].in_use        = 0;
        g_timerfds[slot].interval_ns   = 0;
        g_timerfds[slot].expires_at_ns = 0;
        return 0;
    }
    return -LINUX_EBADF;
}

int64_t linux_eventfd_family_read(int fd, void *buf, size_t len) {
    if (fd_to_slot(fd) >= 0) {
        return linux_eventfd_read(fd, (uint64_t *)buf, len);
    }
    if (signalfd_slot(fd) >= 0) {
        return linux_signalfd_read(fd, buf, len);
    }
    if (tfd_slot(fd) >= 0) {
        return linux_timerfd_read(fd, (uint64_t *)buf, len);
    }
    return -LINUX_EBADF;
}

int64_t linux_eventfd_family_write(int fd, const void *buf, size_t len) {
    if (fd_to_slot(fd) >= 0) {
        if (len < sizeof(uint64_t)) return -LINUX_EINVAL;
        if (!buf) return -LINUX_EFAULT;
        return linux_eventfd_write(fd, *(const uint64_t *)buf, len);
    }
    if (signalfd_slot(fd) >= 0 || tfd_slot(fd) >= 0) {
        return -LINUX_EINVAL;
    }
    return -LINUX_EBADF;
}

int64_t linux_eventfd_family_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (fd_to_slot(fd) >= 0 || signalfd_slot(fd) >= 0 || tfd_slot(fd) >= 0) {
        return -LINUX_ESPIPE;
    }
    return -LINUX_EBADF;
}

static uint64_t ts_to_ns(int64_t sec, int64_t nsec) {
    if (sec < 0 || nsec < 0) return 0;
    return (uint64_t)sec * 1000000000ull + (uint64_t)nsec;
}

static void ns_to_ts(uint64_t ns, int64_t *sec, int64_t *nsec) {
    *sec  = (int64_t)(ns / 1000000000ull);
    *nsec = (int64_t)(ns % 1000000000ull);
}

int64_t linux_timerfd_create(int clockid, uint32_t flags) {
    if (flags & ~LINUX_TFD_KNOWN_FLAGS) return -LINUX_EINVAL;
    if (clockid != LINUX_CLOCK_REALTIME &&
        clockid != LINUX_CLOCK_MONOTONIC &&
        clockid != LINUX_CLOCK_BOOTTIME) {
        return -LINUX_EINVAL;
    }
    for (int i = 0; i < LINUX_TIMERFD_MAX_INSTANCES; i++) {
        if (!g_timerfds[i].in_use) {
            g_timerfds[i].in_use      = 1;
            g_timerfds[i].clockid     = clockid;
            g_timerfds[i].flags       = flags;
            g_timerfds[i].interval_ns = 0;
            g_timerfds[i].expires_at_ns = 0;
            g_timerfds[i].last_read_now_ns = now_ns();
            return (int64_t)(LINUX_TIMERFD_FD_BASE + i);
        }
    }
    return -LINUX_EMFILE;
}

int64_t linux_timerfd_settime(int fd, int flags,
                              const struct linux_itimerspec *new_val,
                              struct linux_itimerspec *old_val) {
    if ((uint32_t)flags & ~LINUX_TFD_SETTIME_KNOWN_FLAGS) return -LINUX_EINVAL;
    int slot = tfd_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    if (!new_val) return -LINUX_EFAULT;

    struct timerfd_slot *t = &g_timerfds[slot];
    uint64_t cur = now_ns();

    if (old_val) {
        /* Linux: old_val gets the *remaining* time. */
        uint64_t remaining =
            (cur < t->expires_at_ns) ? (t->expires_at_ns - cur) : 0;
        ns_to_ts(remaining, &old_val->it_value_sec,
                 &old_val->it_value_nsec);
        ns_to_ts(t->interval_ns, &old_val->it_interval_sec,
                 &old_val->it_interval_nsec);
    }

    uint64_t value_ns = ts_to_ns(new_val->it_value_sec,
                                 new_val->it_value_nsec);
    uint64_t interval_ns = ts_to_ns(new_val->it_interval_sec,
                                    new_val->it_interval_nsec);

    /* Disarm if value == 0. */
    if (value_ns == 0) {
        t->expires_at_ns = 0;
        t->interval_ns   = 0;
    } else {
        if (flags & LINUX_TFD_TIMER_ABSTIME) {
            t->expires_at_ns = value_ns;
        } else {
            t->expires_at_ns = cur + value_ns;
        }
        t->interval_ns = interval_ns;
    }
    t->last_read_now_ns = cur;
    return 0;
}

int64_t linux_timerfd_gettime(int fd, struct linux_itimerspec *cur) {
    int slot = tfd_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    if (!cur) return -LINUX_EFAULT;

    struct timerfd_slot *t = &g_timerfds[slot];
    uint64_t now = now_ns();
    uint64_t remaining =
        (now < t->expires_at_ns) ? (t->expires_at_ns - now) : 0;
    ns_to_ts(remaining, &cur->it_value_sec, &cur->it_value_nsec);
    ns_to_ts(t->interval_ns, &cur->it_interval_sec, &cur->it_interval_nsec);
    return 0;
}

int64_t linux_timerfd_read(int fd, uint64_t *out, size_t len) {
    if (len < sizeof(uint64_t)) return -LINUX_EINVAL;
    if (!out) return -LINUX_EFAULT;
    int slot = tfd_slot(fd);
    if (slot < 0) return -LINUX_EBADF;

    struct timerfd_slot *t = &g_timerfds[slot];
    /* Disarmed timer never fires. */
    if (t->expires_at_ns == 0) return -LINUX_EAGAIN;

    uint64_t now = now_ns();
    if (now < t->expires_at_ns) return -LINUX_EAGAIN;

    /* At least one expiration has happened. Count any periodic
     * extras based on interval_ns. */
    uint64_t expirations = 1;
    if (t->interval_ns > 0) {
        uint64_t past = now - t->expires_at_ns;
        expirations += past / t->interval_ns;
        /* Advance the next expiration to the future. */
        t->expires_at_ns += expirations * t->interval_ns;
        if (t->expires_at_ns <= now) {
            /* Defensive: shouldn't happen with int math above. */
            t->expires_at_ns = now + t->interval_ns;
        }
    } else {
        /* One-shot: disarm. */
        t->expires_at_ns = 0;
    }
    t->last_read_now_ns = now;
    *out = expirations;
    return (int64_t)sizeof(uint64_t);
}

/* ------------ Syscall adapters ------------ */

static int64_t sys_eventfd2(const struct linux_syscall_args *a) {
    return linux_eventfd2(a->a0, (uint32_t)a->a1);
}

static int64_t sys_signalfd4(const struct linux_syscall_args *a) {
    return linux_signalfd4((int)a->a0, a->a1, (size_t)a->a2,
                           (uint32_t)a->a3);
}

static int64_t sys_timerfd_create(const struct linux_syscall_args *a) {
    return linux_timerfd_create((int)a->a0, (uint32_t)a->a1);
}

static int64_t sys_timerfd_settime(const struct linux_syscall_args *a) {
    return linux_timerfd_settime(
        (int)a->a0, (int)a->a1,
        (const struct linux_itimerspec *)(uintptr_t)a->a2,
        (struct linux_itimerspec *)(uintptr_t)a->a3);
}

static int64_t sys_timerfd_gettime(const struct linux_syscall_args *a) {
    return linux_timerfd_gettime(
        (int)a->a0, (struct linux_itimerspec *)(uintptr_t)a->a1);
}

void linux_eventfd_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_eventfd2,        sys_eventfd2);
    (void)linux_syscall_register(LINUX_NR_signalfd4,       sys_signalfd4);
    (void)linux_syscall_register(LINUX_NR_timerfd_create,  sys_timerfd_create);
    (void)linux_syscall_register(LINUX_NR_timerfd_settime, sys_timerfd_settime);
    (void)linux_syscall_register(LINUX_NR_timerfd_gettime, sys_timerfd_gettime);
}
