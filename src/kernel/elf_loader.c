#include "kernel/elf_loader.h"
#include "memory/pmm.h"
#include "memory/kmem.h"
#include "fs/vfs.h"
#include <stddef.h>

static void elf_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

static void elf_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

int elf_validate(const uint8_t *data, size_t size) {
  if (!data || size < sizeof(struct elf64_header)) return -1;
  const struct elf64_header *hdr = (const struct elf64_header *)data;
  if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
      hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') return -1;
  if (hdr->e_ident[4] != 2) return -1;
  if (hdr->e_ident[5] != 1) return -1;
  if (hdr->e_machine != EM_X86_64) return -1;
  if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return -1;
  return 0;
}

int elf_load(struct vmm_address_space *as, const uint8_t *data, size_t size,
             struct elf_load_result *result) {
  if (!as || !data || !result) return -1;
  if (elf_validate(data, size) != 0) return -1;

  const struct elf64_header *hdr = (const struct elf64_header *)data;
  result->entry_point = hdr->e_entry;
  result->phdr_vaddr = 0;
  result->phnum = hdr->e_phnum;
  result->brk = 0;
  result->success = 0;

  uint64_t ph_off = hdr->e_phoff;
  uint16_t ph_size = hdr->e_phentsize;
  uint16_t ph_num = hdr->e_phnum;

  for (uint16_t i = 0; i < ph_num; i++) {
    if (ph_off + (uint64_t)i * ph_size + sizeof(struct elf64_phdr) > size)
      return -1;

    const struct elf64_phdr *phdr =
      (const struct elf64_phdr *)(data + ph_off + (uint64_t)i * ph_size);

    if (phdr->p_type == PT_PHDR) {
      result->phdr_vaddr = phdr->p_vaddr;
      continue;
    }

    if (phdr->p_type != PT_LOAD) continue;

    uint64_t vaddr_start = phdr->p_vaddr & ~(VMM_PAGE_SIZE - 1);
    uint64_t vaddr_end = (phdr->p_vaddr + phdr->p_memsz + VMM_PAGE_SIZE - 1) &
                          ~(VMM_PAGE_SIZE - 1);
    size_t num_pages = (size_t)((vaddr_end - vaddr_start) / VMM_PAGE_SIZE);

    uint64_t flags = VMM_PAGE_USER;
    if (phdr->p_flags & PF_W) flags |= VMM_PAGE_WRITE;
    if (!(phdr->p_flags & PF_X)) flags |= VMM_PAGE_NX;

    for (size_t p = 0; p < num_pages; p++) {
      uint64_t phys = pmm_alloc_page();
      if (!phys) return -1;
      elf_memset((void *)(uintptr_t)phys, 0, VMM_PAGE_SIZE);
      vmm_map_page(as, vaddr_start + p * VMM_PAGE_SIZE, phys, flags);
    }

    if (phdr->p_filesz > 0 && phdr->p_offset + phdr->p_filesz <= size) {
      uint64_t dest_phys = vmm_virt_to_phys(as, phdr->p_vaddr);
      if (dest_phys) {
        elf_memcpy((void *)(uintptr_t)dest_phys,
                   data + phdr->p_offset, (size_t)phdr->p_filesz);
      }
    }

    uint64_t seg_end = phdr->p_vaddr + phdr->p_memsz;
    if (seg_end > result->brk) result->brk = seg_end;
  }

  result->brk = (result->brk + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
  result->success = 1;
  return 0;
}

int elf_load_into_process(struct process *proc, const uint8_t *data,
                          size_t size) {
  if (!proc || !data) return -1;
  if (!proc->address_space) return -1;

  struct elf_load_result result;
  int r = elf_load(proc->address_space, data, size, &result);
  if (r != 0 || !result.success) return -1;

  proc->brk = result.brk;
  proc->heap_start = result.brk;

  /* Eagerly map the top 16 pages of the user stack so the very
   * first user-mode reference to RSP just works without going
   * through the page-fault path. This preserves the phase 5e/5f
   * smoke behaviour bit-for-bit. */
  uint64_t stack_phys = pmm_alloc_pages(16);
  if (!stack_phys) return -1;
  uint64_t stack_base = VMM_USER_STACK - 16 * VMM_PAGE_SIZE;
  vmm_map_range(proc->address_space, stack_base, stack_phys, 16,
                VMM_PAGE_USER | VMM_PAGE_WRITE);
  proc->stack_top = VMM_USER_STACK;

  /* Phase 7b: register the next 240 pages BELOW the eager mapping as
   * an anonymous demand-paged region. A user that grows its stack
   * past the initial 16 pages will fault on the first byte of the
   * 17th page; arch_fault_classify returns ARCH_FAULT_RECOVERABLE
   * for the user #PF P=0; vmm_handle_page_fault finds this region,
   * allocates+zeros a fresh frame, and maps it. The user resumes
   * without observing the fault. Total stack budget is 256 pages
   * (1 MiB), which matches the per-process default expectation
   * documented elsewhere in the tree. The eager region above and
   * this expansion region do not overlap; the registration is
   * therefore guaranteed to succeed under normal kmalloc pressure.
   *
   * Errors are deliberately swallowed: if registration fails, the
   * process simply does not get demand growth (the top 16 pages
   * still work eagerly). This keeps the boot path resilient under
   * pathological memory pressure. */
  const size_t STACK_EXPANSION_PAGES = 240;
  uint64_t stack_expand_top = stack_base;
  uint64_t stack_expand_base = stack_expand_top -
      (uint64_t)STACK_EXPANSION_PAGES * VMM_PAGE_SIZE;
  (void)vmm_register_anon_region(proc->address_space, stack_expand_base,
                                 STACK_EXPANSION_PAGES,
                                 VMM_PAGE_USER | VMM_PAGE_WRITE);

  if (proc->main_thread) {
    proc->main_thread->context.rip = result.entry_point;
    proc->main_thread->context.rsp = proc->stack_top - 8;
  }

  return 0;
}

int elf_load_from_file(struct process *proc, const char *path) {
  if (!proc || !path) return -1;

  struct file *f = vfs_open(path, VFS_OPEN_READ);
  if (!f) return -1;

  struct vfs_stat st;
  if (vfs_stat_path(path, &st) != 0) { vfs_close(f); return -1; }

  size_t fsize = st.size;
  if (fsize < sizeof(struct elf64_header) || fsize > 16 * 1024 * 1024) {
    vfs_close(f);
    return -1;
  }

  uint8_t *buf = (uint8_t *)kmalloc(fsize);
  if (!buf) { vfs_close(f); return -1; }

  long rd = vfs_read(f, buf, fsize);
  vfs_close(f);
  if (rd < 0 || (size_t)rd != fsize) { kfree(buf); return -1; }

  int r = elf_load_into_process(proc, buf, fsize);
  kfree(buf);
  return r;
}
