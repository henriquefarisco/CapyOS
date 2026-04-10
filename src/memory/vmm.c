#include "memory/vmm.h"
#include "memory/pmm.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct vmm_address_space kernel_as;
static struct vmm_stats vmm_global_stats;

static inline void invlpg(uint64_t addr) {
  __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline uint64_t read_cr3(void) {
  uint64_t val;
  __asm__ volatile("movq %%cr3, %0" : "=r"(val));
  return val;
}

static inline void write_cr3(uint64_t val) {
  __asm__ volatile("movq %0, %%cr3" : : "r"(val) : "memory");
}

static uint64_t *get_or_create_table(uint64_t *table, uint32_t index,
                                      uint64_t flags) {
  if (!(table[index] & VMM_PAGE_PRESENT)) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;
    uint8_t *virt = (uint8_t *)(uintptr_t)phys;
    for (int i = 0; i < 4096; i++) virt[i] = 0;
    table[index] = phys | flags | VMM_PAGE_PRESENT;
  }
  return (uint64_t *)(uintptr_t)(table[index] & ~0xFFFULL);
}

void vmm_init(void) {
  kernel_as.pml4_phys = read_cr3() & ~0xFFFULL;
  kernel_as.pml4_virt = (uint64_t *)(uintptr_t)kernel_as.pml4_phys;
  kernel_as.refcount = 1;
  vmm_global_stats.kernel_mapped_pages = 0;
  vmm_global_stats.user_mapped_pages = 0;
  vmm_global_stats.page_faults = 0;
  vmm_global_stats.cow_faults = 0;
}

struct vmm_address_space *vmm_create_address_space(void) {
  struct vmm_address_space *as = (struct vmm_address_space *)kmalloc(sizeof(*as));
  if (!as) return NULL;

  uint64_t pml4_phys = pmm_alloc_page();
  if (!pml4_phys) { kfree(as); return NULL; }

  uint64_t *pml4 = (uint64_t *)(uintptr_t)pml4_phys;
  for (int i = 0; i < 512; i++) pml4[i] = 0;

  uint64_t *kernel_pml4 = kernel_as.pml4_virt;
  for (int i = 256; i < 512; i++) {
    pml4[i] = kernel_pml4[i];
  }

  as->pml4_phys = pml4_phys;
  as->pml4_virt = pml4;
  as->refcount = 1;
  return as;
}

void vmm_destroy_address_space(struct vmm_address_space *as) {
  if (!as || as == &kernel_as) return;
  as->refcount--;
  if (as->refcount > 0) return;

  uint64_t *pml4 = as->pml4_virt;
  for (int i = 0; i < 256; i++) {
    if (!(pml4[i] & VMM_PAGE_PRESENT)) continue;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i] & ~0xFFFULL);
    for (int j = 0; j < 512; j++) {
      if (!(pdpt[j] & VMM_PAGE_PRESENT)) continue;
      uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[j] & ~0xFFFULL);
      for (int k = 0; k < 512; k++) {
        if (!(pd[k] & VMM_PAGE_PRESENT)) continue;
        if (pd[k] & VMM_PAGE_HUGE) {
          pmm_free_page(pd[k] & ~0xFFFULL);
          continue;
        }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[k] & ~0xFFFULL);
        for (int l = 0; l < 512; l++) {
          if (pt[l] & VMM_PAGE_PRESENT) {
            pmm_free_page(pt[l] & ~0xFFFULL);
          }
        }
        pmm_free_page(pd[k] & ~0xFFFULL);
      }
      pmm_free_page(pdpt[j] & ~0xFFFULL);
    }
    pmm_free_page(pml4[i] & ~0xFFFULL);
  }

  pmm_free_page(as->pml4_phys);
  kfree(as);
}

struct vmm_address_space *vmm_kernel_address_space(void) {
  return &kernel_as;
}

void vmm_switch_address_space(struct vmm_address_space *as) {
  if (!as) return;
  uint64_t current = read_cr3() & ~0xFFFULL;
  if (current != as->pml4_phys) {
    write_cr3(as->pml4_phys);
  }
}

int vmm_map_page(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                 uint64_t flags) {
  if (!as) return -1;
  uint64_t *pml4 = as->pml4_virt;

  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  uint64_t table_flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITE;
  if (flags & VMM_PAGE_USER) table_flags |= VMM_PAGE_USER;

  uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, table_flags);
  if (!pdpt) return -1;

  uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, table_flags);
  if (!pd) return -1;

  uint64_t *pt = get_or_create_table(pd, pd_idx, table_flags);
  if (!pt) return -1;

  pt[pt_idx] = (phys & ~0xFFFULL) | flags | VMM_PAGE_PRESENT;
  invlpg(virt);

  if (flags & VMM_PAGE_USER) vmm_global_stats.user_mapped_pages++;
  else vmm_global_stats.kernel_mapped_pages++;

  return 0;
}

int vmm_unmap_page(struct vmm_address_space *as, uint64_t virt) {
  if (!as) return -1;
  uint64_t *pml4 = as->pml4_virt;

  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & ~0xFFFULL);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & ~0xFFFULL);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return -1;
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & ~0xFFFULL);

  pt[pt_idx] = 0;
  invlpg(virt);
  return 0;
}

int vmm_map_range(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                  size_t count, uint64_t flags) {
  for (size_t i = 0; i < count; i++) {
    int r = vmm_map_page(as, virt + i * VMM_PAGE_SIZE,
                         phys + i * VMM_PAGE_SIZE, flags);
    if (r != 0) return r;
  }
  return 0;
}

int vmm_unmap_range(struct vmm_address_space *as, uint64_t virt, size_t count) {
  for (size_t i = 0; i < count; i++) {
    vmm_unmap_page(as, virt + i * VMM_PAGE_SIZE);
  }
  return 0;
}

uint64_t vmm_virt_to_phys(struct vmm_address_space *as, uint64_t virt) {
  if (!as) return 0;
  uint64_t *pml4 = as->pml4_virt;
  uint32_t pml4_idx = (virt >> 39) & 0x1FF;
  uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint32_t pd_idx   = (virt >> 21) & 0x1FF;
  uint32_t pt_idx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_idx] & ~0xFFFULL);
  if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_idx] & ~0xFFFULL);
  if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return 0;
  uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_idx] & ~0xFFFULL);
  if (!(pt[pt_idx] & VMM_PAGE_PRESENT)) return 0;

  return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFF);
}

int vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
  vmm_global_stats.page_faults++;
  (void)fault_addr;
  (void)error_code;
  return -1;
}

void vmm_stats_get(struct vmm_stats *out) {
  if (out) *out = vmm_global_stats;
}
