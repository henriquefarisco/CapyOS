#include "memory/vmm.h"
#include "memory/internal/vmm_internal.h"
#include <assert.h>
#include <stdio.h>

static _Alignas(4096) uint64_t table[512];
static struct vmm_address_space kernel;
static uint64_t mapped_virtual, mapped_physical, mapped_flags;
static size_t mapped_pages;
static unsigned allocation_fails, mapping_fails, unmaps;

uint64_t pmm_alloc_page(void) {
    return allocation_fails ? 0 : (uint64_t)(uintptr_t)table;
}
struct vmm_address_space *vmm_kernel_address_space(void) { return &kernel; }
int vmm_map_range(struct vmm_address_space *as, uint64_t virtual, uint64_t physical,
                  size_t count, uint64_t flags) {
    assert(as == &kernel);
    mapped_virtual = virtual; mapped_physical = physical;
    mapped_pages = count; mapped_flags = flags;
    return mapping_fails ? -1 : 0;
}
int vmm_unmap_range(struct vmm_address_space *as, uint64_t virtual, size_t count) {
    assert(as == &kernel && virtual == mapped_virtual && count == mapped_pages);
    ++unmaps;
    return 0;
}

int main(void) {
    uint64_t root[512] = {0}, user[512] = {0};
    assert(!vmm_map_device(0xc000000000, 4096));
    for (unsigned i = 384; i < 448; ++i) root[i] = 0x123003;
    assert(vmm_prepare_device_window(root) == -1);
    root[385] = 0;
    allocation_fails = 1;
    assert(vmm_prepare_device_window(root) == -1 && root[385] == 0);
    allocation_fails = 0;
    assert(vmm_prepare_device_window(root) == 0);
    assert(root[384] == 0x123003 && root[386] == 0x123003);
    assert((root[385] & 0xfff) == (VMM_PAGE_PRESENT | VMM_PAGE_WRITE));
    assert((root[385] & VMM_PTE_PHYS_MASK) == (uintptr_t)table);
    for (unsigned i = 256; i < 512; ++i) user[i] = root[i];
    assert(user[385] == root[385]); /* same descendant table for existing users */
    assert(vmm_prepare_device_window(root) == -1);
    uint64_t address = (uintptr_t)vmm_map_device(0xc000000123, 4096);
    assert(address && (address & 4095) == 0x123);
    assert(mapped_physical == 0xc000000000 && mapped_pages == 2);
    assert(mapped_virtual == address - 0x123 && (address >> 48) == 0xffff);
    assert(mapped_flags == (VMM_PAGE_WRITE | VMM_PAGE_PCD | VMM_PAGE_PWT | VMM_PAGE_NX));
    assert(!vmm_map_device(0, 1) && !vmm_map_device(1, 0));
    assert(!vmm_map_device(UINT64_MAX - 1, 4096));
    assert(!vmm_map_device((UINT64_C(1) << 52) - 1, 2));
    assert(!vmm_map_device(4096, 16u * 1024u * 1024u + 1));
    mapping_fails = 1;
    assert(!vmm_map_device(0xc000001000, 4096) && unmaps == 1);
    uint64_t failed_address = mapped_virtual;
    mapping_fails = 0;
    assert((uintptr_t)vmm_map_device(0xc000001000, 4096) > failed_address);
    while (vmm_map_device(0xc000000000, 16u * 1024u * 1024u)) {}
    assert(!vmm_map_device(0xc000000000, 16u * 1024u * 1024u));
    puts("[vmm-mmio] shared supervisor window, UC/NX, bounds and failure cleanup: PASS");
    return 0;
}
