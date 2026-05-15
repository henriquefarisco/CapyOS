#include "kernel/linux_compat/linux_modern_misc.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static uint8_t g_memfd_secret_in_use[LINUX_MEMFD_SECRET_FD_MAX];
static int     g_initialised;

static void ensure_init(void) {
    if (g_initialised) return;
    for (int i = 0; i < LINUX_MEMFD_SECRET_FD_MAX; i++) {
        g_memfd_secret_in_use[i] = 0;
    }
    g_initialised = 1;
}

static int memfd_secret_slot(int fd) {
    ensure_init();
    int slot = fd - LINUX_MEMFD_SECRET_FD_BASE;
    if (slot < 0 || slot >= LINUX_MEMFD_SECRET_FD_MAX) return -1;
    if (!g_memfd_secret_in_use[slot]) return -1;
    return slot;
}

void linux_modern_misc_reset_for_tests(void) {
    g_initialised = 0;
    ensure_init();
}

int64_t linux_futex_waitv(void *waiters, uint32_t nr_futexes,
                          uint32_t flags, void *timeout, int clockid) {
    (void)timeout;
    if (flags != 0) return -LINUX_EINVAL;
    if (nr_futexes == 0) return -LINUX_EINVAL;
    if (nr_futexes > LINUX_FUTEX_WAITV_MAX) return -LINUX_EINVAL;
    if (!waiters) return -LINUX_EFAULT;
    if (clockid != LINUX_FX_CLOCK_REALTIME &&
        clockid != LINUX_FX_CLOCK_MONOTONIC) {
        return -LINUX_EINVAL;
    }
    /* Marco M1: musl falls back to single-futex FUTEX_WAIT loop
     * (which we have wired in linux_futex). Clean -ENOSYS path. */
    return -LINUX_ENOSYS;
}

int64_t linux_clock_adjtime(int clk_id, struct linux_timex_subset *buf) {
    if (clk_id < 0) return -LINUX_EINVAL;
    if (!buf) return -LINUX_EFAULT;
    /* Linux: kernel rejects modes that aren't a known subset. We
     * only need to accept ADJ_SETOFFSET / ADJ_OFFSET no-op style
     * for chrony's "are you alive?" probe. The caller's modes
     * field tells us what they want. */
    if (buf->modes & ~LINUX_TIMEX_MOD_KNOWN) return -LINUX_EINVAL;
    /* Read-only path or write-as-no-op: report TIME_OK. */
    /* Linux returns the leap-second state, not 0; TIME_OK == 0
     * so this is consistent. */
    return LINUX_TIME_OK;
}

int64_t linux_memfd_secret(uint32_t flags) {
    ensure_init();
    if (flags & ~LINUX_MEMFD_SECRET_KNOWN) return -LINUX_EINVAL;
    /* Linux: requires CAP_IPC_LOCK; root has it implicitly. */
    for (int i = 0; i < LINUX_MEMFD_SECRET_FD_MAX; i++) {
        if (!g_memfd_secret_in_use[i]) {
            g_memfd_secret_in_use[i] = 1;
            return LINUX_MEMFD_SECRET_FD_BASE + i;
        }
    }
    return -LINUX_ENFILE;
}

int64_t linux_memfd_secret_close(int fd) {
    int slot = memfd_secret_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_memfd_secret_in_use[slot] = 0;
    return 0;
}

int64_t linux_memfd_secret_read(int fd, void *buf, size_t len) {
    if (memfd_secret_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_ENOSYS;
}

int64_t linux_memfd_secret_write(int fd, const void *buf, size_t len) {
    if (memfd_secret_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_ENOSYS;
}

int64_t linux_memfd_secret_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (memfd_secret_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_ENOSYS;
}

static int64_t sys_futex_waitv(const struct linux_syscall_args *a) {
    return linux_futex_waitv((void *)(uintptr_t)a->a0,
                             (uint32_t)a->a1, (uint32_t)a->a2,
                             (void *)(uintptr_t)a->a3,
                             (int)a->a4);
}
static int64_t sys_clock_adjtime(const struct linux_syscall_args *a) {
    return linux_clock_adjtime((int)a->a0,
        (struct linux_timex_subset *)(uintptr_t)a->a1);
}
static int64_t sys_memfd_secret(const struct linux_syscall_args *a) {
    return linux_memfd_secret((uint32_t)a->a0);
}

void linux_modern_misc_register_syscalls(void) {
    ensure_init();
    (void)linux_syscall_register(LINUX_NR_futex_waitv,   sys_futex_waitv);
    (void)linux_syscall_register(LINUX_NR_clock_adjtime, sys_clock_adjtime);
    (void)linux_syscall_register(LINUX_NR_memfd_secret,  sys_memfd_secret);
}
