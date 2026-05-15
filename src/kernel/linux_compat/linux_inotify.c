#include "kernel/linux_compat/linux_inotify.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

struct watch {
    int      wd;
    uint32_t mask;
    /* Path stored as a fixed-cap string for diagnostics. */
    char     path[64];
};

struct inotify_instance {
    int       in_use;
    uint32_t  flags;
    int       next_wd;        /* monotonic wd allocator (1..) */
    size_t    count;
    struct watch watches[LINUX_INOTIFY_MAX_PER_INSTANCE];
};

static struct inotify_instance g_inotify[LINUX_INOTIFY_MAX_INSTANCES];

void linux_inotify_reset_for_tests(void) {
    for (size_t i = 0; i < LINUX_INOTIFY_MAX_INSTANCES; i++) {
        g_inotify[i].in_use  = 0;
        g_inotify[i].next_wd = 1;
        g_inotify[i].count   = 0;
    }
}

static int fd_to_slot(int fd) {
    int slot = fd - LINUX_INOTIFY_FD_BASE;
    if (slot < 0 || slot >= LINUX_INOTIFY_MAX_INSTANCES) return -1;
    if (!g_inotify[slot].in_use) return -1;
    return slot;
}

static struct watch *find_watch(struct inotify_instance *e, int wd) {
    for (size_t i = 0; i < e->count; i++) {
        if (e->watches[i].wd == wd) return &e->watches[i];
    }
    return NULL;
}

/* ---------- inotify_init1 ---------- */

int64_t linux_inotify_init1(uint32_t flags) {
    if (flags & ~LINUX_IN_INIT_KNOWN_FLAGS) return -LINUX_EINVAL;
    for (size_t i = 0; i < LINUX_INOTIFY_MAX_INSTANCES; i++) {
        if (!g_inotify[i].in_use) {
            g_inotify[i].in_use  = 1;
            g_inotify[i].flags   = flags;
            g_inotify[i].next_wd = 1;
            g_inotify[i].count   = 0;
            return (int64_t)(LINUX_INOTIFY_FD_BASE + (int)i);
        }
    }
    return -LINUX_EMFILE;
}

/* ---------- inotify_add_watch ---------- */

int64_t linux_inotify_add_watch(int fd, uint64_t path_ptr, uint32_t mask) {
    if (path_ptr == 0) return -LINUX_EFAULT;
    if (mask == 0) return -LINUX_EINVAL;
    if (mask & ~LINUX_IN_KNOWN_MASK) return -LINUX_EINVAL;
    /* Linux requires at least one event bit. */
    if ((mask & LINUX_IN_ALL_EVENTS) == 0) return -LINUX_EINVAL;

    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    struct inotify_instance *e = &g_inotify[slot];
    if (e->count >= LINUX_INOTIFY_MAX_PER_INSTANCE) return -LINUX_ENOMEM;

    struct watch *w = &e->watches[e->count++];
    w->wd   = e->next_wd++;
    w->mask = mask;
    /* Copy path, truncating to fit. */
    const char *src = (const char *)(uintptr_t)path_ptr;
    size_t i = 0;
    while (i + 1 < sizeof(w->path) && src[i] != '\0') {
        w->path[i] = src[i];
        i++;
    }
    w->path[i] = '\0';
    return (int64_t)w->wd;
}

/* ---------- inotify_rm_watch ---------- */

int64_t linux_inotify_rm_watch(int fd, int wd) {
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    struct inotify_instance *e = &g_inotify[slot];
    struct watch *w = find_watch(e, wd);
    if (!w) return -LINUX_EINVAL;
    /* Swap-with-last removal. */
    *w = e->watches[e->count - 1];
    e->count--;
    return 0;
}

int64_t linux_inotify_close(int fd) {
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_inotify[slot].in_use  = 0;
    g_inotify[slot].flags   = 0;
    g_inotify[slot].next_wd = 1;
    g_inotify[slot].count   = 0;
    return 0;
}

int64_t linux_inotify_read(int fd, void *buf, size_t len) {
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EAGAIN;
}

int64_t linux_inotify_write(int fd, const void *buf, size_t len) {
    if (fd_to_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_inotify_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (fd_to_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_ESPIPE;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_inotify_init1(const struct linux_syscall_args *a) {
    return linux_inotify_init1((uint32_t)a->a0);
}
static int64_t sys_inotify_add_watch(const struct linux_syscall_args *a) {
    return linux_inotify_add_watch((int)a->a0, a->a1, (uint32_t)a->a2);
}
static int64_t sys_inotify_rm_watch(const struct linux_syscall_args *a) {
    return linux_inotify_rm_watch((int)a->a0, (int)a->a1);
}

void linux_inotify_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_inotify_init1,     sys_inotify_init1);
    (void)linux_syscall_register(LINUX_NR_inotify_add_watch, sys_inotify_add_watch);
    (void)linux_syscall_register(LINUX_NR_inotify_rm_watch,  sys_inotify_rm_watch);
}
