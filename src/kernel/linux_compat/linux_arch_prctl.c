#include "kernel/linux_compat/linux_arch_prctl.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_arch_prctl_ops g_ops;
static int                         g_ops_installed;

void linux_arch_prctl_install_ops(const struct linux_arch_prctl_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_arch_prctl_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_arch_prctl_reset_for_tests(void) {
    g_ops = (struct linux_arch_prctl_ops){0};
    g_ops_installed = 0;
}

int64_t linux_arch_prctl(int op, uint64_t arg) {
    switch (op) {
      case LINUX_ARCH_SET_FS:
        if (!g_ops_installed || !g_ops.set_fs_base) return -LINUX_ENOSYS;
        g_ops.set_fs_base(arg);
        return 0;

      case LINUX_ARCH_SET_GS:
        if (!g_ops_installed || !g_ops.set_gs_base) return -LINUX_ENOSYS;
        g_ops.set_gs_base(arg);
        return 0;

      case LINUX_ARCH_GET_FS: {
        if (arg == 0) return -LINUX_EFAULT;
        if (!g_ops_installed || !g_ops.get_fs_base) return -LINUX_ENOSYS;
        uint64_t v = g_ops.get_fs_base();
        *(uint64_t *)(uintptr_t)arg = v;
        return 0;
      }

      case LINUX_ARCH_GET_GS: {
        if (arg == 0) return -LINUX_EFAULT;
        if (!g_ops_installed || !g_ops.get_gs_base) return -LINUX_ENOSYS;
        uint64_t v = g_ops.get_gs_base();
        *(uint64_t *)(uintptr_t)arg = v;
        return 0;
      }

      default:
        return -LINUX_EINVAL;
    }
}

static int64_t sys_arch_prctl(const struct linux_syscall_args *a) {
    return linux_arch_prctl((int)(int32_t)a->a0, a->a1);
}

void linux_arch_prctl_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_arch_prctl, sys_arch_prctl);
}
