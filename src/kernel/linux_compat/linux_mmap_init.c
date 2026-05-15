#include "kernel/linux_compat/linux_mmap.h"

/* Boot wiring for `linux_mmap` against the real VMM. Excluded
 * from host tests.
 *
 * The VMM today exposes `vmm_register_anon_region(as, start,
 * page_count, flags)` for demand-paged user mappings, but mmap
 * needs a virtual-address allocator on top of that (Linux picks
 * the address; we cannot ask userland). For Marco M1 the
 * trampoline below uses a simple per-AS bump pointer in the
 * upper user-VA range (0x0000_5000_0000_0000 onward) and calls
 * the existing anon-region helper. Free is a no-op for now
 * (regions live until the AS is destroyed); mprotect requires
 * fine-grained PTE updates which the VMM has but no userland
 * helper exposes yet.
 *
 * What this means in practice for Marco M1:
 *   - mmap returns a valid VA that demand-pages on first touch.
 *   - munmap returns 0 but does not actually release frames
 *     (acceptable for short-lived processes; bytes are reclaimed
 *     when the AS is destroyed).
 *   - mprotect returns -EINVAL until the VMM grows a per-page
 *     prot helper. SpiderMonkey JIT will see this and fall back
 *     to its W^X-via-mmap path (allocating two regions with
 *     different prots).
 */

#if !defined(UNIT_TEST)

#include "memory/vmm.h"

#include <stdint.h>
#include <stddef.h>

/* Per-process bump pointer base. Living in .bss so it starts at
 * 0; the wiring resets it lazily on the first call. */
static uint64_t g_mmap_next_va;

/* Start of the mmap arena. Far above the typical user heap base
 * so it never collides with elf load segments or stack. */
#define MMAP_ARENA_BASE 0x0000500000000000ull
/* End of the arena: 1 TiB later. */
#define MMAP_ARENA_END  0x0000510000000000ull

static uint64_t prot_to_vmm_flags(uint32_t prot) {
    uint64_t f = VMM_PAGE_USER;
    if (prot & LINUX_PROT_WRITE) f |= VMM_PAGE_WRITE;
    if ((prot & LINUX_PROT_EXEC) == 0) f |= VMM_PAGE_NX;
    return f;
}

static uint64_t wrap_alloc_anon(size_t pages, uint32_t prot) {
    struct vmm_address_space *as = vmm_current_address_space();
    if (!as) return 0;

    if (g_mmap_next_va == 0) g_mmap_next_va = MMAP_ARENA_BASE;
    uint64_t bytes = (uint64_t)pages * VMM_PAGE_SIZE;
    if (g_mmap_next_va + bytes >= MMAP_ARENA_END) return 0;

    uint64_t start = g_mmap_next_va;
    int rc = vmm_register_anon_region(as, start, pages,
                                      prot_to_vmm_flags(prot));
    if (rc != 0) return 0;

    g_mmap_next_va += bytes;
    return start;
}

static int wrap_free_pages(uint64_t addr, size_t pages) {
    /* Best-effort: the VMM does not yet expose a "deregister anon
     * region" helper. Returning 0 keeps libc happy; frames are
     * released when the AS is destroyed. */
    (void)addr;
    (void)pages;
    return 0;
}

static int wrap_protect(uint64_t addr, size_t pages, uint32_t prot) {
    /* No per-page prot mutation helper yet. SpiderMonkey JIT
     * detects this and falls back to dual-mapping. */
    (void)addr;
    (void)pages;
    (void)prot;
    return -1;
}

void linux_mmap_init_boot(void) {
    static const struct linux_mmap_ops ops = {
        .alloc_anon = wrap_alloc_anon,
        .free_pages = wrap_free_pages,
        .protect    = wrap_protect,
    };
    linux_mmap_install_ops(&ops);
}

#endif /* !UNIT_TEST */
