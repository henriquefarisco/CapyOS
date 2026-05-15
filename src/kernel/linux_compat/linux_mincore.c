#include "kernel/linux_compat/linux_mincore.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

int64_t linux_mincore(uintptr_t addr, size_t length, uint8_t *vec) {
    /* Linux: addr must be page-aligned. */
    if (addr & (LINUX_MINCORE_PAGE_SIZE - 1)) return -LINUX_EINVAL;
    if (length == 0) {
        /* Linux: length 0 is a no-op success. vec is not
         * touched. */
        return 0;
    }
    /* Overflow check: addr + length must not wrap. */
    if (addr + length < addr) return -LINUX_ENOMEM;
    if (!vec) return -LINUX_EFAULT;
    /* Number of pages: ceil(length / page_size). */
    size_t pages = (length + LINUX_MINCORE_PAGE_SIZE - 1) /
                   LINUX_MINCORE_PAGE_SIZE;
    /* Marco M1: no swap, no page aging. All pages resident.
     * Bit 0 = present, bit 7 = locked (we leave 0 as we don't
     * track mlock state per-page yet). */
    for (size_t i = 0; i < pages; i++) {
        vec[i] = 1;
    }
    return 0;
}

static int64_t sys_mincore(const struct linux_syscall_args *a) {
    return linux_mincore((uintptr_t)a->a0, (size_t)a->a1,
                         (uint8_t *)(uintptr_t)a->a2);
}

void linux_mincore_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_mincore, sys_mincore);
}
