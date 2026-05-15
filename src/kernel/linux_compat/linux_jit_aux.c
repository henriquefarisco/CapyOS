#include "kernel/linux_compat/linux_jit_aux.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static uint8_t g_uffd_in_use[LINUX_UFFD_FD_MAX];
static int     g_uffd_initialised;
static uint32_t g_membarrier_registered;

static void ensure_uffd_init(void) {
    if (g_uffd_initialised) return;
    for (int i = 0; i < LINUX_UFFD_FD_MAX; i++) g_uffd_in_use[i] = 0;
    g_uffd_initialised = 1;
}

void linux_jit_aux_reset_for_tests(void) {
    for (int i = 0; i < LINUX_UFFD_FD_MAX; i++) g_uffd_in_use[i] = 0;
    g_uffd_initialised = 1;
    g_membarrier_registered = 0;
}

static int uffd_slot(int fd) {
    ensure_uffd_init();
    int slot = fd - LINUX_UFFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_UFFD_FD_MAX) return -1;
    if (!g_uffd_in_use[slot]) return -1;
    return slot;
}

int64_t linux_membarrier(int cmd, uint32_t flags, int cpu_id) {
    (void)cpu_id;
    if (cmd == LINUX_MEMBARRIER_CMD_QUERY) {
        if (flags) return -LINUX_EINVAL;
        return LINUX_MEMBARRIER_SUPPORTED;
    }
    if (flags & ~LINUX_MEMBARRIER_FLAG_KNOWN) return -LINUX_EINVAL;
    /* Linux: REGISTER_* commands set up the per-task state for
     * later barrier issuance. We track which kinds were
     * registered so the EXPEDITED variants can fail-fast on
     * unregistered usage like Linux does. */
    if (cmd == LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED ||
        cmd == LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE ||
        cmd == LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED) {
        g_membarrier_registered |= (uint32_t)cmd;
        return 0;
    }
    if (cmd == LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED ||
        cmd == LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE) {
        /* Linux: requires REGISTER_PRIVATE_EXPEDITED first. */
        if (!(g_membarrier_registered &
              LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED)) {
            return -LINUX_EPERM;
        }
        /* Marco M1 single-task: no IPI needed. Issue full
         * compiler barrier semantically; cooperative scheduler
         * + UP execution makes it a no-op. */
        return 0;
    }
    if (cmd == LINUX_MEMBARRIER_CMD_GLOBAL ||
        cmd == LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED) {
        return 0;
    }
    return -LINUX_EINVAL;
}

int64_t linux_userfaultfd(int flags) {
    if ((unsigned)flags & ~LINUX_UFFD_KNOWN_FLAGS) return -LINUX_EINVAL;
    ensure_uffd_init();
    for (int i = 0; i < LINUX_UFFD_FD_MAX; i++) {
        if (!g_uffd_in_use[i]) {
            g_uffd_in_use[i] = 1;
            return LINUX_UFFD_FD_BASE + i;
        }
    }
    /* Linux: ENFILE when system-wide table is exhausted. */
    return -LINUX_ENFILE;
}

int64_t linux_userfaultfd_close(int fd) {
    int slot = uffd_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_uffd_in_use[slot] = 0;
    return 0;
}

int64_t linux_userfaultfd_read(int fd, void *buf, size_t len) {
    if (uffd_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EAGAIN;
}

int64_t linux_userfaultfd_write(int fd, const void *buf, size_t len) {
    if (uffd_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_userfaultfd_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (uffd_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_ESPIPE;
}

int64_t linux_sched_rr_get_interval(int pid, struct linux_jit_timespec *tp) {
    if (pid < 0) return -LINUX_EINVAL;
    if (!tp) return -LINUX_EFAULT;
    /* Linux default RR slice is 100 ms. We mirror that until
     * the cooperative scheduler grows real RT slices. */
    tp->tv_sec = 0;
    tp->tv_nsec = 100000000;  /* 100 ms */
    return 0;
}

static int64_t sys_membarrier(const struct linux_syscall_args *a) {
    return linux_membarrier((int)a->a0, (uint32_t)a->a1, (int)a->a2);
}
static int64_t sys_userfaultfd(const struct linux_syscall_args *a) {
    return linux_userfaultfd((int)a->a0);
}
static int64_t sys_rr_get_interval(const struct linux_syscall_args *a) {
    return linux_sched_rr_get_interval((int)a->a0,
        (struct linux_jit_timespec *)(uintptr_t)a->a1);
}

void linux_jit_aux_register_syscalls(void) {
    ensure_uffd_init();
    (void)linux_syscall_register(LINUX_NR_membarrier,    sys_membarrier);
    (void)linux_syscall_register(LINUX_NR_userfaultfd,   sys_userfaultfd);
    (void)linux_syscall_register(LINUX_NR_sched_rr_get_interval,
                                 sys_rr_get_interval);
}
