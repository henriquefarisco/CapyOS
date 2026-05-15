#include "kernel/linux_compat/linux_umask.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>

static uint32_t g_umask = 0022u;

uint32_t linux_umask(uint32_t mask) {
    uint32_t prev = g_umask;
    g_umask = mask & LINUX_UMASK_MASK_BITS;
    return prev;
}

void linux_umask_reset_for_tests(void) {
    g_umask = 0022u;
}

static int64_t sys_umask(const struct linux_syscall_args *a) {
    return (int64_t)linux_umask((uint32_t)a->a0);
}

void linux_umask_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_umask, sys_umask);
}
