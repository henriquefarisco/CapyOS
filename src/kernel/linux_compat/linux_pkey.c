#include "kernel/linux_compat/linux_pkey.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* keys 0 and 1 are reserved by Linux convention (default key
 * and execute-only key); userland keys start at 2. */
#define LINUX_PKEY_MIN_USER  2

static uint8_t g_in_use[LINUX_PKEY_MAX];
static int     g_initialised;

static void ensure_init(void) {
    if (g_initialised) return;
    for (int i = 0; i < LINUX_PKEY_MAX; i++) g_in_use[i] = 0;
    /* Linux: keys 0 and 1 are pre-allocated by the kernel and
     * cannot be returned by pkey_alloc. */
    g_in_use[0] = 1;
    g_in_use[1] = 1;
    g_initialised = 1;
}

void linux_pkey_reset_for_tests(void) {
    g_initialised = 0;
    ensure_init();
}

int64_t linux_pkey_alloc(uint32_t flags, uint32_t access_rights) {
    ensure_init();
    if (flags != 0) return -LINUX_EINVAL;
    if (access_rights & ~LINUX_PKEY_ACCESS_KNOWN) return -LINUX_EINVAL;
    for (int i = LINUX_PKEY_MIN_USER; i < LINUX_PKEY_MAX; i++) {
        if (!g_in_use[i]) {
            g_in_use[i] = 1;
            return i;
        }
    }
    return -LINUX_ENOSPC;
}

int64_t linux_pkey_free(int pkey) {
    ensure_init();
    if (pkey < LINUX_PKEY_MIN_USER || pkey >= LINUX_PKEY_MAX) {
        return -LINUX_EINVAL;
    }
    if (!g_in_use[pkey]) return -LINUX_EINVAL;
    g_in_use[pkey] = 0;
    return 0;
}

int64_t linux_pkey_mprotect(uintptr_t addr, size_t len, int prot, int pkey) {
    /* addr must be page-aligned (4 KiB on x86_64). */
    if (addr & 0xFFFu) return -LINUX_EINVAL;
    /* Linux: len rounded up internally; len 0 -> 0 success. */
    if (len == 0) return 0;
    if ((unsigned)prot & ~(unsigned)LINUX_PROT_KNOWN_BITS) {
        return -LINUX_EINVAL;
    }
    /* Linux: pkey == -1 means "use the default key". */
    if (pkey != -1) {
        if (pkey < 0 || pkey >= LINUX_PKEY_MAX) return -LINUX_EINVAL;
        ensure_init();
        if (!g_in_use[pkey]) return -LINUX_EINVAL;
    }
    /* Marco M1: no real per-page key state. The mprotect
     * upgrade itself is a no-op success since we don't enforce
     * page protection in the cooperative single-task world. */
    return 0;
}

static int64_t sys_alloc(const struct linux_syscall_args *a) {
    return linux_pkey_alloc((uint32_t)a->a0, (uint32_t)a->a1);
}
static int64_t sys_free(const struct linux_syscall_args *a) {
    return linux_pkey_free((int)a->a0);
}
static int64_t sys_mprotect(const struct linux_syscall_args *a) {
    return linux_pkey_mprotect((uintptr_t)a->a0, (size_t)a->a1,
                               (int)a->a2, (int)a->a3);
}

void linux_pkey_register_syscalls(void) {
    ensure_init();
    (void)linux_syscall_register(LINUX_NR_pkey_alloc,    sys_alloc);
    (void)linux_syscall_register(LINUX_NR_pkey_free,     sys_free);
    (void)linux_syscall_register(LINUX_NR_pkey_mprotect, sys_mprotect);
}
