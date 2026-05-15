#include "kernel/linux_compat/linux_ioctl.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>

int64_t linux_ioctl(int fd, uint32_t cmd, uint64_t arg) {
    (void)cmd;
    (void)arg;
    if (fd < 0) return -LINUX_EBADF;
    /* Marco M1: no terminal/socket/framebuffer support yet, so
     * every ioctl(non_negative_fd, *) reports "not a typewriter".
     * This is the Linux behaviour for ioctls on regular files
     * and is what musl's stdio init keys off to pick block-
     * buffered mode. */
    return -LINUX_ENOTTY;
}

static int64_t sys_ioctl(const struct linux_syscall_args *a) {
    return linux_ioctl((int)a->a0, (uint32_t)a->a1, a->a2);
}

void linux_ioctl_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_ioctl, sys_ioctl);
}
