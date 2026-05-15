#include "kernel/linux_compat/linux_brk.h"

/* Boot wiring for `linux_brk` against the real VMM. Excluded
 * from host tests via UNIT_TEST.
 *
 * Strategy: when userland grows its heap break, we call
 * `vmm_register_anon_region` for the new pages. The VMM only
 * accepts non-overlapping registrations, so we register one
 * region per growth event (typically 4 KiB chunks for early
 * malloc, 64 KiB-1 MiB for later). RW + USER + NX. */

#if !defined(UNIT_TEST)

#include "memory/vmm.h"

#include <stdint.h>
#include <stddef.h>

static int wrap_reserve_pages(uint64_t start_va, size_t pages) {
    if (pages == 0) return 0;
    struct vmm_address_space *as = vmm_current_address_space();
    if (!as) return -1;
    uint64_t flags = VMM_PAGE_USER | VMM_PAGE_WRITE | VMM_PAGE_NX;
    return vmm_register_anon_region(as, start_va, pages, flags);
}

void linux_brk_init_boot(void) {
    static const struct linux_brk_ops ops = {
        .reserve_pages = wrap_reserve_pages,
    };
    linux_brk_install_ops(&ops);
}

#else /* UNIT_TEST */

void linux_brk_init_boot(void) {}

#endif
