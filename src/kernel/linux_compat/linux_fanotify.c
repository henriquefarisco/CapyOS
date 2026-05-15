#include "kernel/linux_compat/linux_fanotify.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static uint8_t g_fan_in_use[LINUX_FAN_FD_MAX];
static int     g_initialised;

static void ensure_init(void) {
    if (g_initialised) return;
    for (int i = 0; i < LINUX_FAN_FD_MAX; i++) g_fan_in_use[i] = 0;
    g_initialised = 1;
}

void linux_fanotify_reset_for_tests(void) {
    g_initialised = 0;
    ensure_init();
}

static int valid_fan_fd(int fd) {
    ensure_init();
    if (fd < LINUX_FAN_FD_BASE) return 0;
    int idx = fd - LINUX_FAN_FD_BASE;
    if (idx >= LINUX_FAN_FD_MAX) return 0;
    return g_fan_in_use[idx];
}

static int fan_fd_slot(int fd) {
    ensure_init();
    if (fd < LINUX_FAN_FD_BASE) return -1;
    int idx = fd - LINUX_FAN_FD_BASE;
    if (idx < 0 || idx >= LINUX_FAN_FD_MAX) return -1;
    if (!g_fan_in_use[idx]) return -1;
    return idx;
}

int64_t linux_fanotify_init(uint32_t flags, uint32_t event_f_flags) {
    ensure_init();
    if (flags & ~LINUX_FAN_INIT_KNOWN) return -LINUX_EINVAL;
    /* event_f_flags is an open(2) flags subset; we don't enforce
     * a strict whitelist here -- the kernel only validates RDWR/
     * RDONLY/WRONLY + a few common bits. */
    (void)event_f_flags;
    /* Linux: requires CAP_SYS_ADMIN; root has it implicitly. */
    for (int i = 0; i < LINUX_FAN_FD_MAX; i++) {
        if (!g_fan_in_use[i]) {
            g_fan_in_use[i] = 1;
            return LINUX_FAN_FD_BASE + i;
        }
    }
    return -LINUX_ENFILE;
}

int64_t linux_fanotify_mark(int fan_fd, uint32_t flags,
                            uint64_t mask, int dirfd,
                            const char *pathname) {
    ensure_init();
    (void)mask; (void)dirfd;
    if (!valid_fan_fd(fan_fd)) return -LINUX_EBADF;
    if (flags & ~LINUX_FAN_MARK_KNOWN) return -LINUX_EINVAL;
    /* ADD and REMOVE are mutually exclusive (and FLUSH overrides
     * both per Linux fs/notify/fanotify/fanotify_user.c). */
    int has_add = (flags & LINUX_FAN_MARK_ADD) ? 1 : 0;
    int has_rem = (flags & LINUX_FAN_MARK_REMOVE) ? 1 : 0;
    int has_flush = (flags & LINUX_FAN_MARK_FLUSH) ? 1 : 0;
    if (has_add && has_rem) return -LINUX_EINVAL;
    if (has_flush && (has_add || has_rem)) return -LINUX_EINVAL;
    if (!has_add && !has_rem && !has_flush) return -LINUX_EINVAL;
    /* FLUSH ignores pathname; ADD/REMOVE expect pathname or dirfd
     * with empty path. */
    if (!has_flush && !pathname) return -LINUX_EFAULT;
    /* Marco M1: no real fanotify backend; accept structurally. */
    return 0;
}

int64_t linux_fanotify_close(int fd) {
    int slot = fan_fd_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_fan_in_use[slot] = 0;
    return 0;
}

int64_t linux_fanotify_read(int fd, void *buf, size_t len) {
    if (fan_fd_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EAGAIN;
}

int64_t linux_fanotify_write(int fd, const void *buf, size_t len) {
    if (fan_fd_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_fanotify_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (fan_fd_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_ESPIPE;
}

static int64_t sys_init(const struct linux_syscall_args *a) {
    return linux_fanotify_init((uint32_t)a->a0, (uint32_t)a->a1);
}
static int64_t sys_mark(const struct linux_syscall_args *a) {
    return linux_fanotify_mark((int)a->a0, (uint32_t)a->a1,
                               (uint64_t)a->a2, (int)a->a3,
                               (const char *)(uintptr_t)a->a4);
}

void linux_fanotify_register_syscalls(void) {
    ensure_init();
    (void)linux_syscall_register(LINUX_NR_fanotify_init, sys_init);
    (void)linux_syscall_register(LINUX_NR_fanotify_mark, sys_mark);
}
