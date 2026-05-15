#include "kernel/linux_compat/linux_memfd.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

struct memfd_slot {
    int      in_use;
    char     name[LINUX_MEMFD_NAME_MAX + 1];
    uint32_t flags;
};

struct pidfd_slot {
    int      in_use;
    uint32_t pid;
    uint32_t flags;
};

static struct memfd_slot     g_memfds[LINUX_MEMFD_MAX_INSTANCES];
static struct pidfd_slot     g_pidfds[LINUX_PIDFD_MAX_INSTANCES];
static struct linux_memfd_ops g_ops;

void linux_memfd_install_ops(const struct linux_memfd_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_memfd_ops){0};
}

void linux_memfd_reset_for_tests(void) {
    for (size_t i = 0; i < LINUX_MEMFD_MAX_INSTANCES; i++) {
        g_memfds[i].in_use = 0;
    }
    for (size_t i = 0; i < LINUX_PIDFD_MAX_INSTANCES; i++) {
        g_pidfds[i].in_use = 0;
    }
    g_ops = (struct linux_memfd_ops){0};
}

/* ---------- memfd_create ---------- */

static size_t name_len(const char *s, size_t cap) {
    size_t i = 0;
    while (i < cap && s[i] != '\0') i++;
    return i;
}

int64_t linux_memfd_create(uint64_t name_ptr, uint32_t flags) {
    if (flags & ~LINUX_MFD_KNOWN_FLAGS) return -LINUX_EINVAL;
    if (name_ptr == 0) return -LINUX_EFAULT;

    const char *name = (const char *)(uintptr_t)name_ptr;
    /* Linux requires name length <= 249. */
    size_t len = name_len(name, LINUX_MEMFD_NAME_MAX + 1);
    if (len > LINUX_MEMFD_NAME_MAX) return -LINUX_EINVAL;

    for (size_t i = 0; i < LINUX_MEMFD_MAX_INSTANCES; i++) {
        if (!g_memfds[i].in_use) {
            g_memfds[i].in_use = 1;
            for (size_t j = 0; j < len; j++) g_memfds[i].name[j] = name[j];
            g_memfds[i].name[len] = '\0';
            g_memfds[i].flags = flags;
            return (int64_t)(LINUX_MEMFD_FD_BASE + (int)i);
        }
    }
    return -LINUX_EMFILE;
}

/* ---------- pidfd_open ---------- */

int64_t linux_pidfd_open(uint32_t pid, uint32_t flags) {
    if (flags & ~LINUX_PIDFD_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* Linux: pid 0 is invalid for pidfd_open (use 0 only with
     * pidfd_send_signal as 'self'). */
    if (pid == 0) return -LINUX_EINVAL;

    if (g_ops.pid_exists && !g_ops.pid_exists(pid)) {
        return -LINUX_ESRCH;
    }

    for (size_t i = 0; i < LINUX_PIDFD_MAX_INSTANCES; i++) {
        if (!g_pidfds[i].in_use) {
            g_pidfds[i].in_use = 1;
            g_pidfds[i].pid    = pid;
            g_pidfds[i].flags  = flags;
            return (int64_t)(LINUX_PIDFD_FD_BASE + (int)i);
        }
    }
    return -LINUX_EMFILE;
}

static int memfd_slot(int fd) {
    int slot = fd - LINUX_MEMFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_MEMFD_MAX_INSTANCES) return -1;
    if (!g_memfds[slot].in_use) return -1;
    return slot;
}

static int pidfd_slot(int fd) {
    int slot = fd - LINUX_PIDFD_FD_BASE;
    if (slot < 0 || slot >= LINUX_PIDFD_MAX_INSTANCES) return -1;
    if (!g_pidfds[slot].in_use) return -1;
    return slot;
}

int64_t linux_memfd_family_close(int fd) {
    int slot = memfd_slot(fd);
    if (slot >= 0) {
        g_memfds[slot].in_use = 0;
        g_memfds[slot].flags  = 0;
        g_memfds[slot].name[0] = '\0';
        return 0;
    }
    slot = pidfd_slot(fd);
    if (slot >= 0) {
        g_pidfds[slot].in_use = 0;
        g_pidfds[slot].pid    = 0;
        g_pidfds[slot].flags  = 0;
        return 0;
    }
    return -LINUX_EBADF;
}

int64_t linux_memfd_family_read(int fd, void *buf, size_t len) {
    if (memfd_slot(fd) >= 0) {
        if (len == 0) return 0;
        if (!buf) return -LINUX_EFAULT;
        return -LINUX_ENOSYS;
    }
    if (pidfd_slot(fd) >= 0) {
        if (len == 0) return 0;
        if (!buf) return -LINUX_EFAULT;
        return -LINUX_EINVAL;
    }
    return -LINUX_EBADF;
}

int64_t linux_memfd_family_write(int fd, const void *buf, size_t len) {
    if (memfd_slot(fd) >= 0) {
        if (len == 0) return 0;
        if (!buf) return -LINUX_EFAULT;
        return -LINUX_ENOSYS;
    }
    if (pidfd_slot(fd) >= 0) {
        if (len == 0) return 0;
        if (!buf) return -LINUX_EFAULT;
        return -LINUX_EINVAL;
    }
    return -LINUX_EBADF;
}

int64_t linux_memfd_family_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (memfd_slot(fd) >= 0) return -LINUX_ENOSYS;
    if (pidfd_slot(fd) >= 0) return -LINUX_ESPIPE;
    return -LINUX_EBADF;
}

/* ---------- pidfd_send_signal ---------- */

int64_t linux_pidfd_send_signal(int pidfd, int sig,
                                uint64_t info_ptr, uint32_t flags) {
    (void)info_ptr;
    if (flags & ~LINUX_PIDFD_SS_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* Linux signal numbers are 1..64; sig 0 is "no-op probe". */
    if (sig < 0 || sig > 64) return -LINUX_EINVAL;

    int slot = pidfd_slot(pidfd);
    if (slot < 0) return -LINUX_EBADF;

    /* Without delivery infra, sig != 0 cannot fire. sig == 0 can
     * be used as a "is the process still alive?" probe; we return
     * 0 if pid still exists. */
    if (sig == 0) {
        if (g_ops.pid_exists && !g_ops.pid_exists(g_pidfds[slot].pid)) {
            return -LINUX_ESRCH;
        }
        return 0;
    }
    return -LINUX_ENOSYS;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_memfd_create(const struct linux_syscall_args *a) {
    return linux_memfd_create(a->a0, (uint32_t)a->a1);
}

static int64_t sys_pidfd_open(const struct linux_syscall_args *a) {
    return linux_pidfd_open((uint32_t)a->a0, (uint32_t)a->a1);
}

static int64_t sys_pidfd_send_signal(const struct linux_syscall_args *a) {
    return linux_pidfd_send_signal((int)a->a0, (int)a->a1,
                                   a->a2, (uint32_t)a->a3);
}

void linux_memfd_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_memfd_create,      sys_memfd_create);
    (void)linux_syscall_register(LINUX_NR_pidfd_open,        sys_pidfd_open);
    (void)linux_syscall_register(LINUX_NR_pidfd_send_signal, sys_pidfd_send_signal);
}
