#include "kernel/linux_compat/linux_random.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static linux_random_bytes_fn g_bytes_fn = NULL;

void linux_random_install_source(linux_random_bytes_fn fn) {
    g_bytes_fn = fn;
}

void linux_random_reset_for_tests(void) {
    g_bytes_fn = NULL;
}

int64_t linux_getrandom(void *buf, size_t len, uint32_t flags) {
    if (flags & ~(uint32_t)LINUX_GRND_KNOWN_MASK) return -LINUX_EINVAL;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    if (!g_bytes_fn) return -LINUX_EAGAIN;

    /* Linux clips len at INT_MAX-ish; mirror so userland that retries
     * on short reads agrees with us. */
    if (len > (size_t)LINUX_GETRANDOM_INT_MAX) {
        len = (size_t)LINUX_GETRANDOM_INT_MAX;
    }

    g_bytes_fn(buf, len);
    return (int64_t)len;
}

/* ------------------------------------------------------------------ */
/* Syscall adapter.                                                   */
/* ------------------------------------------------------------------ */

static int64_t linux_syscall_getrandom(const struct linux_syscall_args *a) {
    void    *buf   = (void *)(uintptr_t)a->a0;
    size_t   len   = (size_t)a->a1;
    uint32_t flags = (uint32_t)a->a2;
    return linux_getrandom(buf, len, flags);
}

void linux_random_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getrandom, linux_syscall_getrandom);
}
