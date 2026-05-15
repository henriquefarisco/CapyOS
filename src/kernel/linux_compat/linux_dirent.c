#include "kernel/linux_compat/linux_dirent.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

int64_t linux_getdents64(int fd, void *dirp, size_t count) {
    if (fd < 0)                  return -LINUX_EBADF;
    if (count == 0)              return 0;
    if (count > 0 && dirp == NULL) return -LINUX_EFAULT;
    /* Marco M1: no directory fd table, no enumerable entries.
     * Return 0 = EOF so userland readdir() reports empty
     * directory cleanly. */
    return 0;
}

static int64_t sys_getdents64(const struct linux_syscall_args *a) {
    return linux_getdents64((int)a->a0,
                            (void *)(uintptr_t)a->a1,
                            (size_t)a->a2);
}

void linux_dirent_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getdents64, sys_getdents64);
}
