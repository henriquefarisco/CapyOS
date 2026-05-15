#include "kernel/linux_compat/linux_creds.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

void linux_creds_reset_for_tests(void) {
    /* Marco M1 has no per-task credential storage yet; reset is
     * a no-op today but keeps the hook stable for the
     * provider-injection migration. */
}

int64_t linux_getgroups(int size, uint32_t *list) {
    if (size < 0) return -LINUX_EINVAL;
    /* Marco M1: zero supplementary groups. The Linux idiom
     * `getgroups(0, NULL)` returns the count without writing,
     * which is exactly what a zero-group answer requires. */
    if (size == 0) return 0;
    if (!list) return -LINUX_EFAULT;
    /* size > 0 with a valid buffer: report zero count and don't
     * touch the buffer. */
    return 0;
}

int64_t linux_setgroups(size_t size, const uint32_t *list) {
    if (size > LINUX_NGROUPS_MAX) return -LINUX_EINVAL;
    if (size > 0 && !list) return -LINUX_EFAULT;
    /* Marco M1 runs as root with no supplementary groups. We
     * accept any well-formed list and discard it; this is what
     * Linux does when the caller has CAP_SETGID, which root
     * implicitly has. When per-task credentials land, this
     * stores into the task's `cred->groups`. */
    return 0;
}

static int64_t sys_getgroups(const struct linux_syscall_args *a) {
    return linux_getgroups((int)a->a0, (uint32_t *)(uintptr_t)a->a1);
}
static int64_t sys_setgroups(const struct linux_syscall_args *a) {
    return linux_setgroups((size_t)a->a0,
                           (const uint32_t *)(uintptr_t)a->a1);
}

void linux_creds_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getgroups, sys_getgroups);
    (void)linux_syscall_register(LINUX_NR_setgroups, sys_setgroups);
}
