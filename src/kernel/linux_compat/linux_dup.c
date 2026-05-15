#include "kernel/linux_compat/linux_dup.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_dup_ops g_ops;
static int                  g_ops_installed;

void linux_dup_install_ops(const struct linux_dup_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_dup_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_dup_reset_for_tests(void) {
    g_ops = (struct linux_dup_ops){0};
    g_ops_installed = 0;
}

int64_t linux_dup(int oldfd) {
    if (oldfd < 0) return -LINUX_EBADF;
    if (g_ops_installed && g_ops.dup) return g_ops.dup(oldfd);
    return -LINUX_ENOSYS;
}

int64_t linux_dup2(int oldfd, int newfd) {
    if (oldfd < 0)  return -LINUX_EBADF;
    if (newfd < 0)  return -LINUX_EBADF;
    /* Linux semantics: if oldfd == newfd, dup2 returns newfd
     * without doing anything (assuming oldfd is valid). We
     * accept any non-negative oldfd as "valid enough" in Marco
     * M1; a real fd table check lands when the table does. */
    if (oldfd == newfd) return newfd;
    if (g_ops_installed && g_ops.dup2) return g_ops.dup2(oldfd, newfd);
    return -LINUX_ENOSYS;
}

static int64_t sys_dup(const struct linux_syscall_args *a) {
    return linux_dup((int)a->a0);
}

static int64_t sys_dup2(const struct linux_syscall_args *a) {
    return linux_dup2((int)a->a0, (int)a->a1);
}

void linux_dup_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_dup, sys_dup);
    (void)linux_syscall_register(LINUX_NR_dup2, sys_dup2);
}
