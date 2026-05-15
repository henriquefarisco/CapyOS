#include "kernel/linux_compat/linux_lock.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

#define LINUX_LOCK_TABLE_SIZE 32

struct lock_slot {
    int      fd;       /* -1 == empty */
    uint8_t  mode;     /* LOCK_SH or LOCK_EX */
};

static struct lock_slot       g_locks[LINUX_LOCK_TABLE_SIZE];
static int                    g_locks_initialised;
static struct linux_lock_ops  g_ops;
static int                    g_ops_installed;

static void ensure_initialised(void) {
    if (g_locks_initialised) return;
    for (int i = 0; i < LINUX_LOCK_TABLE_SIZE; i++) {
        g_locks[i].fd = -1;
        g_locks[i].mode = 0;
    }
    g_locks_initialised = 1;
}

void linux_lock_install_ops(const struct linux_lock_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_lock_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_lock_reset_for_tests(void) {
    g_ops = (struct linux_lock_ops){0};
    for (int i = 0; i < LINUX_LOCK_TABLE_SIZE; i++) {
        g_locks[i].fd = -1;
        g_locks[i].mode = 0;
    }
    g_locks_initialised = 1;
    g_ops_installed = 0;
}

static int find_slot(int fd) {
    for (int i = 0; i < LINUX_LOCK_TABLE_SIZE; i++) {
        if (g_locks[i].fd == fd) return i;
    }
    return -1;
}

static int alloc_slot(int fd) {
    for (int i = 0; i < LINUX_LOCK_TABLE_SIZE; i++) {
        if (g_locks[i].fd == -1) {
            g_locks[i].fd = fd;
            return i;
        }
    }
    return -1;
}

int64_t linux_flock(int fd, int operation) {
    ensure_initialised();
    if (fd < 0) return -LINUX_EBADF;
    /* Reject unknown bits before mode parsing. */
    if ((unsigned)operation & ~(unsigned)LINUX_LOCK_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    int mode = operation & LINUX_LOCK_MODE_MASK;
    /* Linux: exactly one of LOCK_SH/LOCK_EX/LOCK_UN must be set
     * (they have distinct values, but their bitwise OR -- e.g.
     * LOCK_SH | LOCK_EX -- is invalid). */
    if (mode != LINUX_LOCK_SH && mode != LINUX_LOCK_EX &&
        mode != LINUX_LOCK_UN) {
        return -LINUX_EINVAL;
    }
    /* LOCK_NB combined with LOCK_UN is harmless (Linux ignores
     * NB in unlock path). */

    int idx = find_slot(fd);

    if (mode == LINUX_LOCK_UN) {
        if (idx >= 0) {
            g_locks[idx].fd = -1;
            g_locks[idx].mode = 0;
        }
        /* Unlocking an unlocked fd is a no-op success. */
        return 0;
    }

    if (idx < 0) {
        idx = alloc_slot(fd);
        /* Marco M1: 32-slot lock table. If we exhausted it we
         * surface -ENOLCK so userland sees a fail-closed signal
         * instead of silently losing the lock. */
        if (idx < 0) return -LINUX_ENOLCK;
    }
    g_locks[idx].mode = (uint8_t)mode;
    /* Single-process world: no contention; LOCK_NB cannot fail
     * here. */
    return 0;
}

int64_t linux_copy_file_range(int fd_in, int64_t *off_in,
                              int fd_out, int64_t *off_out,
                              size_t len, uint32_t flags) {
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;
    /* Linux: flags must be 0. */
    if (flags != 0) return -LINUX_EINVAL;
    if (g_ops_installed && g_ops.copy_file_range) {
        return g_ops.copy_file_range(fd_in, off_in, fd_out, off_out,
                                     len, flags);
    }
    /* No backend: userland falls back to read+write. */
    return -LINUX_ENOSYS;
}

static int64_t sys_flock(const struct linux_syscall_args *a) {
    return linux_flock((int)a->a0, (int)a->a1);
}
static int64_t sys_copy_file_range(const struct linux_syscall_args *a) {
    return linux_copy_file_range((int)a->a0,
                                 (int64_t *)(uintptr_t)a->a1,
                                 (int)a->a2,
                                 (int64_t *)(uintptr_t)a->a3,
                                 (size_t)a->a4,
                                 (uint32_t)a->a5);
}

void linux_lock_register_syscalls(void) {
    ensure_initialised();
    (void)linux_syscall_register(LINUX_NR_flock,           sys_flock);
    (void)linux_syscall_register(LINUX_NR_copy_file_range, sys_copy_file_range);
}
