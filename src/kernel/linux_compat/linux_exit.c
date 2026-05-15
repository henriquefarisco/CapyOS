#include "kernel/linux_compat/linux_exit.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_exit_ops g_ops;
static int                   g_ops_installed;

void linux_exit_install_ops(const struct linux_exit_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_exit_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_exit_reset_for_tests(void) {
    g_ops = (struct linux_exit_ops){0};
    g_ops_installed = 0;
}

int64_t linux_exit(int code) {
    if (!g_ops_installed || !g_ops.exit_task) {
        /* Production must always have ops installed. The test
         * scenario without ops -- so the test can observe the
         * missing-callback path -- returns ENOSYS as a sentinel.
         * Real userland never reaches this. */
        return -LINUX_ENOSYS;
    }
    g_ops.exit_task(code);
    /* If exit_task did NOT terminate (i.e. test stub returned
     * via longjmp or just returned), surface code & 0xFF Linux-
     * style. Real production task_exit is noreturn so this is
     * unreachable on the kernel. */
    return (int64_t)(code & 0xFF);
}

int64_t linux_exit_group(int code) {
    /* Single-thread model: identical to linux_exit. When S1.4
     * thread groups land, this iterates the tg and kills all
     * threads first; for now that's a no-op. */
    return linux_exit(code);
}

static int64_t sys_exit(const struct linux_syscall_args *a) {
    return linux_exit((int)(int32_t)a->a0);
}

static int64_t sys_exit_group(const struct linux_syscall_args *a) {
    return linux_exit_group((int)(int32_t)a->a0);
}

void linux_exit_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_exit,       sys_exit);
    (void)linux_syscall_register(LINUX_NR_exit_group, sys_exit_group);
}
