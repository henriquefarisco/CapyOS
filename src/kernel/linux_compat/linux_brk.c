#include "kernel/linux_compat/linux_brk.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

#define VMM_PAGE_SIZE_LOCAL 4096ull

static struct linux_brk_ops g_ops;
static int                  g_ops_installed;
static uint64_t             g_brk_current = LINUX_BRK_BASE;
static uint64_t             g_brk_committed = LINUX_BRK_BASE;

void linux_brk_install_ops(const struct linux_brk_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_brk_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_brk_reset_for_tests(void) {
    g_ops = (struct linux_brk_ops){0};
    g_ops_installed     = 0;
    g_brk_current       = LINUX_BRK_BASE;
    g_brk_committed     = LINUX_BRK_BASE;
}

uint64_t linux_brk_current(void) { return g_brk_current; }

static uint64_t page_round_up(uint64_t v) {
    return (v + VMM_PAGE_SIZE_LOCAL - 1) & ~(VMM_PAGE_SIZE_LOCAL - 1);
}

int64_t linux_brk(uint64_t new_break) {
    /* brk(0) -- query mode. Always returns the live break. */
    if (new_break == 0) return (int64_t)g_brk_current;

    /* Out of range below or above heap arena: Linux returns the
     * existing break unchanged (failure indicator). */
    if (new_break < LINUX_BRK_BASE) return (int64_t)g_brk_current;
    if (new_break > LINUX_BRK_BASE + LINUX_BRK_MAX_SIZE) {
        return (int64_t)g_brk_current;
    }

    /* Shrink case: just retract the live break. We do NOT release
     * frames -- Linux is allowed to retain them, and dropping
     * them today would require a `vmm_unregister_anon_region`
     * we don't expose yet. */
    if (new_break <= g_brk_current) {
        g_brk_current = new_break;
        return (int64_t)g_brk_current;
    }

    /* Grow case: round the new break up to the next page; reserve
     * any freshly-needed pages via the injected callback. */
    uint64_t want_committed = page_round_up(new_break);
    if (want_committed > g_brk_committed) {
        if (!g_ops_installed || !g_ops.reserve_pages) {
            /* No reserve impl -> behave as if reservation failed.
             * Linux returns the existing break unchanged. */
            return (int64_t)g_brk_current;
        }
        size_t pages_to_add =
            (size_t)((want_committed - g_brk_committed) / VMM_PAGE_SIZE_LOCAL);
        int rc = g_ops.reserve_pages(g_brk_committed, pages_to_add);
        if (rc != 0) {
            /* Reservation failed: keep the old break visible to
             * userland. malloc will switch to mmap. */
            return (int64_t)g_brk_current;
        }
        g_brk_committed = want_committed;
    }
    g_brk_current = new_break;
    return (int64_t)g_brk_current;
}

static int64_t sys_brk(const struct linux_syscall_args *a) {
    return linux_brk(a->a0);
}

void linux_brk_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_brk, sys_brk);
}
