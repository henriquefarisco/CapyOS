#include "kernel/linux_compat/linux_time_legacy.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_time_legacy_ops g_ops;
static int                          g_ops_installed;

void linux_time_legacy_install_ops(const struct linux_time_legacy_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_time_legacy_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_time_legacy_reset_for_tests(void) {
    g_ops = (struct linux_time_legacy_ops){0};
    g_ops_installed = 0;
}

int64_t linux_time(int64_t *tloc) {
    int64_t now = (g_ops_installed && g_ops.now_seconds)
                  ? g_ops.now_seconds() : 0;
    if (tloc) *tloc = now;
    return now;
}

int64_t linux_getcpu(uint32_t *cpu, uint32_t *node) {
    /* Linux: NULL pointers are silently accepted. The third
     * argument (struct getcpu_cache *) is unused since 2.6.24. */
    if (cpu)  *cpu  = 0;
    if (node) *node = 0;
    return 0;
}

static int64_t sys_time(const struct linux_syscall_args *a) {
    return linux_time((int64_t *)(uintptr_t)a->a0);
}
static int64_t sys_getcpu(const struct linux_syscall_args *a) {
    return linux_getcpu((uint32_t *)(uintptr_t)a->a0,
                        (uint32_t *)(uintptr_t)a->a1);
}

void linux_time_legacy_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_time,   sys_time);
    (void)linux_syscall_register(LINUX_NR_getcpu, sys_getcpu);
}
