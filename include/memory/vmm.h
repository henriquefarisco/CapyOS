#ifndef MEMORY_VMM_H
#define MEMORY_VMM_H

#include <stdint.h>
#include <stddef.h>

#define VMM_PAGE_SIZE 4096
#define VMM_PAGE_PRESENT  (1ULL << 0)
#define VMM_PAGE_WRITE    (1ULL << 1)
#define VMM_PAGE_USER     (1ULL << 2)
#define VMM_PAGE_PWT      (1ULL << 3)
#define VMM_PAGE_PCD      (1ULL << 4)
#define VMM_PAGE_ACCESSED (1ULL << 5)
#define VMM_PAGE_DIRTY    (1ULL << 6)
#define VMM_PAGE_HUGE     (1ULL << 7)
#define VMM_PAGE_GLOBAL   (1ULL << 8)
#define VMM_PAGE_NX       (1ULL << 63)

#define VMM_KERNEL_BASE 0xFFFF800000000000ULL
#define VMM_USER_BASE   0x0000000000400000ULL
#define VMM_USER_TOP    0x00007FFFFFFFF000ULL
#define VMM_USER_STACK  0x00007FFFFFF00000ULL

struct vmm_address_space {
  uint64_t pml4_phys;
  uint64_t *pml4_virt;
  uint32_t refcount;
};

struct vmm_mapping {
  uint64_t virt_start;
  uint64_t phys_start;
  size_t page_count;
  uint64_t flags;
  struct vmm_mapping *next;
};

struct vmm_stats {
  uint64_t kernel_mapped_pages;
  uint64_t user_mapped_pages;
  uint64_t page_faults;
  uint64_t cow_faults;
};

void vmm_init(void);
struct vmm_address_space *vmm_create_address_space(void);
void vmm_destroy_address_space(struct vmm_address_space *as);
struct vmm_address_space *vmm_kernel_address_space(void);
void vmm_switch_address_space(struct vmm_address_space *as);
int vmm_map_page(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                 uint64_t flags);
int vmm_unmap_page(struct vmm_address_space *as, uint64_t virt);
int vmm_map_range(struct vmm_address_space *as, uint64_t virt, uint64_t phys,
                  size_t count, uint64_t flags);
int vmm_unmap_range(struct vmm_address_space *as, uint64_t virt, size_t count);
uint64_t vmm_virt_to_phys(struct vmm_address_space *as, uint64_t virt);
int vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code);
void vmm_stats_get(struct vmm_stats *out);

#endif /* MEMORY_VMM_H */
