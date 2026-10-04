#include "memory/vmm.h"
#include "memory/pmm.h"
#include "memory/internal/vmm_internal.h"

#define DEVICE_WINDOW_BYTES (64u * 1024u * 1024u)
#define DEVICE_MAPPING_MAX (16u * 1024u * 1024u)
static uint64_t device_base, device_used;
static uint32_t device_gate;

/* Run once on kernel-owned boot tables, before any user address space exists.
 * Its high-half PML4 entry is then inherited by every address space; later
 * device mappings modify shared descendants, not a private low-half clone.
 * Preserve all firmware entries and fail closed if no reserved slot is free. */
int vmm_prepare_device_window(uint64_t *pml4) {
    if (!pml4 || device_base) return -1;
    unsigned slot;
    for (slot = 384; slot < 448 && pml4[slot] != 0; ++slot) {}
    if (slot == 448) return -1;
    uint64_t physical = pmm_alloc_page();
    if (!physical) return -1;
    uint64_t *table = (uint64_t *)(uintptr_t)physical;
    for (unsigned i = 0; i < 512; ++i) table[i] = 0;
    pml4[slot] = physical | VMM_PAGE_PRESENT | VMM_PAGE_WRITE;
    device_base = UINT64_C(0xffff000000000000) | (uint64_t)slot << 39;
    return 0;
}

void *vmm_map_device(uint64_t physical, size_t bytes) {
    const uint64_t physical_limit = UINT64_C(1) << 52;
    if (!device_base || !physical || !bytes || bytes > DEVICE_MAPPING_MAX ||
        physical >= physical_limit || bytes > physical_limit - physical)
        return NULL;
    uint64_t offset = physical & (VMM_PAGE_SIZE - 1u);
    uint64_t span = (offset + bytes + VMM_PAGE_SIZE - 1u) &
                    ~(uint64_t)(VMM_PAGE_SIZE - 1u);
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&device_gate, &expected, 1u, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return NULL;
    void *result = NULL;
    if (span <= DEVICE_WINDOW_BYTES - device_used) {
        uint64_t address = device_base + device_used;
        device_used += span; /* failed ranges also remain reserved */
        struct vmm_address_space *as = vmm_kernel_address_space();
        size_t pages = (size_t)(span / VMM_PAGE_SIZE);
        if (as && vmm_map_range(as, address, physical - offset, pages,
                VMM_PAGE_WRITE | VMM_PAGE_PCD | VMM_PAGE_PWT | VMM_PAGE_NX) == 0)
            result = (void *)(uintptr_t)(address + offset);
        else if (as)
            (void)vmm_unmap_range(as, address, pages);
    }
    __atomic_store_n(&device_gate, 0u, __ATOMIC_RELEASE);
    return result;
}
