#include "kernel/linux_compat/linux_mlock.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static int64_t mlock_validate(uint64_t addr, size_t len) {
    if (len == 0) return 0;
    /* Linux: addr+len wrap-around -> -EINVAL. */
    uint64_t end = addr + (uint64_t)len;
    if (end < addr) return -LINUX_EINVAL;
    return 0;
}

int64_t linux_mlock(uint64_t addr, size_t len) {
    int64_t rc = mlock_validate(addr, len);
    if (rc < 0) return rc;
    /* Marco M1 has no swap; pages are already pinned. */
    return 0;
}

int64_t linux_munlock(uint64_t addr, size_t len) {
    int64_t rc = mlock_validate(addr, len);
    if (rc < 0) return rc;
    return 0;
}

int64_t linux_mlockall(int flags) {
    if (flags == 0) return -LINUX_EINVAL; /* Linux: at least one bit required */
    if ((unsigned)flags & ~(unsigned)LINUX_MCL_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    return 0;
}

int64_t linux_munlockall(void) {
    return 0;
}

static int64_t sys_mlock(const struct linux_syscall_args *a) {
    return linux_mlock(a->a0, (size_t)a->a1);
}
static int64_t sys_munlock(const struct linux_syscall_args *a) {
    return linux_munlock(a->a0, (size_t)a->a1);
}
static int64_t sys_mlockall(const struct linux_syscall_args *a) {
    return linux_mlockall((int)a->a0);
}
static int64_t sys_munlockall(const struct linux_syscall_args *a) {
    (void)a;
    return linux_munlockall();
}

void linux_mlock_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_mlock,      sys_mlock);
    (void)linux_syscall_register(LINUX_NR_munlock,    sys_munlock);
    (void)linux_syscall_register(LINUX_NR_mlockall,   sys_mlockall);
    (void)linux_syscall_register(LINUX_NR_munlockall, sys_munlockall);
}
