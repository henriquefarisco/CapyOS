#include "kernel/elf_loader.h"
#include "kernel/elf_bounds.h"
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

#ifdef CAPYOS_ELFLOAD_RO_DIAG
#include "memory/vmm.h"
#include "drivers/serial/serial_com1.h"
static void elf_diag_hex(uint64_t v) {
  static const char h[] = "0123456789ABCDEF";
  char b[17];
  int i;
  for (i = 0; i < 16; i++) b[i] = h[(v >> ((15 - i) * 4)) & 0xFu];
  b[16] = '\0';
  com1_puts(b);
}
/* alpha.311 diagnostic: elf_load writes segment bytes into freshly-allocated
 * frames THROUGH THE KERNEL IDENTITY MAP. On VMware, some frame handed out on
 * the 2nd browser open is read-only in the (firmware-owned) identity map at
 * write time -> #PF. This checks the target's writability first; if read-only
 * it LOGS phys/vaddr on COM1 and returns 0 so the caller SKIPS the write
 * (avoids the kernel panic so the full picture reaches the serial log and the
 * host survives). Gated: production builds are unchanged. */
static int elf_diag_write_ok(uint64_t phys, uint64_t vaddr, const char *tag) {
  if (vmm_identity_is_writable(phys)) return 1;
  com1_puts("[elf-diag] read-only target ");
  com1_puts(tag);
  com1_puts(" phys=0x");
  elf_diag_hex(phys);
  com1_puts(" vaddr=0x");
  elf_diag_hex(vaddr);
  com1_puts("\n");
  return 0;
}
/* Log once: the CR3 elf_load was called on vs the kernel's own PML4. Confirms
 * on a VMware run whether the caller was on a process AS (prev != kernel) and
 * that vmm_enter_kernel_tables switched to the kernel's RW tables. */
static void elf_diag_log_ctx_once(uint64_t prev_cr3) {
  static int logged = 0;
  if (logged) return;
  logged = 1;
  com1_puts("[elf-diag] ctx prev_cr3=0x");
  elf_diag_hex(prev_cr3 & ~0xFFFULL);
  com1_puts(" kernel_pml4=0x");
  elf_diag_hex(vmm_kernel_pml4_phys());
  com1_puts("\n");
}
#endif

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

  /* e_phoff/e_phentsize/e_phnum come straight from the untrusted file; use
   * the overflow-safe helper so a crafted value cannot wrap past the bound
   * and turn `phdr` into a wild pointer. */
  for (uint16_t i = 0; i < ph_num; i++) {
    uint64_t entry_off = (uint64_t)i * ph_size;
    if (!elf_phdr_entry_fits(ph_off, entry_off, sizeof(struct elf64_phdr),
                             size))
      return -1;

    const struct elf64_phdr *phdr =
      (const struct elf64_phdr *)(data + ph_off + entry_off);

    if (phdr->p_type == PT_PHDR) {
      result->phdr_vaddr = phdr->p_vaddr;
      continue;
    }

    if (phdr->p_type != PT_LOAD) continue;

    /* Reject a segment whose virtual span wraps uint64 (crafted p_vaddr +
     * p_memsz); otherwise vaddr_end below wraps and num_pages underflows
     * into an enormous mapping loop. */
    if (!elf_sum_no_wrap(phdr->p_vaddr, phdr->p_memsz)) return -1;

    /* The virtual span is untrusted; require it inside the user half
     * [0, VMM_USER_TOP]. Without this a crafted p_vaddr near UINT64_MAX makes
     * the `+ VMM_PAGE_SIZE - 1` page rounding below wrap (huge num_pages ->
     * runaway mapping loop / memory exhaustion), and a kernel-half p_vaddr
     * would install USER-flagged PTEs over the kernel-half page tables
     * (vmm_map_page does not range-check the virtual address). VMM_USER_TOP is
     * far below UINT64_MAX, so the rounding cannot overflow once this passes. */
    if (!elf_vaddr_in_user_range(phdr->p_vaddr, phdr->p_memsz, VMM_USER_TOP))
      return -1;

    uint64_t vaddr_start = phdr->p_vaddr & ~(VMM_PAGE_SIZE - 1);
    uint64_t vaddr_end = (phdr->p_vaddr + phdr->p_memsz + VMM_PAGE_SIZE - 1) &
                          ~(VMM_PAGE_SIZE - 1);
    size_t num_pages = (size_t)((vaddr_end - vaddr_start) / VMM_PAGE_SIZE);

    uint64_t flags = VMM_PAGE_USER;
    if (phdr->p_flags & PF_W) flags |= VMM_PAGE_WRITE;
    if (!(phdr->p_flags & PF_X)) flags |= VMM_PAGE_NX;

    /* alpha.311: run this segment's frame writes on the kernel's OWN page
     * tables. elf_load writes via the identity map ((void*)phys); on VMware the
     * caller's address space can have that frame read-only, but the kernel's
     * own tables (vmm_build_kernel_tables) map all RAM RW. vmm_map_page /
     * vmm_virt_to_phys below still target `as` correctly (they reach its tables
     * by physical address, which stays identity-mapped in kernel_as). */
    uint64_t elf_prev_cr3 = vmm_enter_kernel_tables();
#ifdef CAPYOS_ELFLOAD_RO_DIAG
    elf_diag_log_ctx_once(elf_prev_cr3);
#endif
    for (size_t p = 0; p < num_pages; p++) {
      uint64_t phys = pmm_alloc_page();
      if (!phys) {
        vmm_leave_kernel_tables(elf_prev_cr3);
        return -1;
      }
#ifdef CAPYOS_ELFLOAD_RO_DIAG
      if (elf_diag_write_ok(phys, vaddr_start + p * VMM_PAGE_SIZE, "memset"))
#endif
        elf_memset((void *)(uintptr_t)phys, 0, VMM_PAGE_SIZE);
      vmm_map_page(as, vaddr_start + p * VMM_PAGE_SIZE, phys, flags);
    }

    /* 2026-05-01: copia pagina-a-pagina. As paginas fisicas alocadas
     * acima por pmm_alloc_page() NAO sao contiguas, entao um unico
     * vmm_virt_to_phys(p_vaddr) + memcpy(filesz) so esta correto se
     * filesz <= 4 KiB. Para binarios maiores (legacy browser, capysh
     * com mais codigo) o memcpy extrapola a primeira pagina fisica
     * e corrompe RAM alheia, causando #GP/#PF longe do site original.
     * O loop abaixo recolhe o phys de cada pagina e copia apenas
     * o slice que cabe nela. p_offset alinhamento dentro da pagina
     * e respeitado via `page_off`. */
    if (phdr->p_filesz > 0 &&
        elf_range_in_bounds(phdr->p_offset, phdr->p_filesz, size)) {
      uint64_t copied = 0;
      while (copied < phdr->p_filesz) {
        uint64_t cur_vaddr = phdr->p_vaddr + copied;
        uint64_t page_base = cur_vaddr & ~(VMM_PAGE_SIZE - 1);
        uint64_t page_off = cur_vaddr - page_base;
        uint64_t page_remain = VMM_PAGE_SIZE - page_off;
        uint64_t to_copy = phdr->p_filesz - copied;
        if (to_copy > page_remain) to_copy = page_remain;
        uint64_t page_phys = vmm_virt_to_phys(as, page_base);
        if (!page_phys) break;
#ifdef CAPYOS_ELFLOAD_RO_DIAG
        if (elf_diag_write_ok(page_phys, page_base, "memcpy"))
#endif
          elf_memcpy((void *)(uintptr_t)(page_phys + page_off),
                     data + phdr->p_offset + copied, (size_t)to_copy);
        copied += to_copy;
      }
    }
    /* alpha.311: restore the caller's address space now that this segment's
     * frame writes are done. */
    vmm_leave_kernel_tables(elf_prev_cr3);

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
    proc->main_thread->cr3 = proc->address_space->pml4_phys;
    proc->main_thread->context.cr3 = proc->address_space->pml4_phys;
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
