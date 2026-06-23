#ifndef KERNEL_ELF_BOUNDS_H
#define KERNEL_ELF_BOUNDS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Overflow-safe bounds helpers for the ELF loader.
 *
 * ELF header fields (e_phoff, e_phentsize, e_phnum, p_offset, p_filesz,
 * p_vaddr, p_memsz) are untrusted: a crafted binary can set them anywhere in
 * the 64-bit range, including values near UINT64_MAX chosen specifically to
 * make a naive `a + b <= size` bound wrap around and pass. These predicates
 * use subtraction only -- they never add two attacker-controlled 64-bit
 * values -- so they cannot wrap. Pure, header-only and host-testable
 * (tests/kernel/test_elf_bounds.c) so the loader's bounds logic is locked
 * against regression independently of the kernel-only loader TU.
 */

/* True iff a program-header entry of `entry_size` bytes located at
 * `ph_off + entry_off` lies fully within a `size`-byte image. */
static inline int elf_phdr_entry_fits(uint64_t ph_off, uint64_t entry_off,
                                      uint64_t entry_size, uint64_t size) {
  if (ph_off > size) return 0;
  if (entry_off > size - ph_off) return 0;
  if (entry_size > size - ph_off - entry_off) return 0;
  return 1;
}

/* True iff the byte range [off, off + span) lies fully within a `size`-byte
 * image (used for a PT_LOAD segment's file-backed bytes). */
static inline int elf_range_in_bounds(uint64_t off, uint64_t span,
                                      uint64_t size) {
  if (off > size) return 0;
  if (span > size - off) return 0;
  return 1;
}

/* True iff `base + span` does not wrap uint64 (used for a segment's virtual
 * span p_vaddr + p_memsz before page rounding). */
static inline int elf_sum_no_wrap(uint64_t base, uint64_t span) {
  return base <= ~(uint64_t)0 - span;
}

/* True iff a segment's virtual span [vaddr, vaddr + memsz) lies fully within
 * the user range [0, user_top]. Subtraction-only (no wrap). `p_vaddr`/`p_memsz`
 * are untrusted: this rejects (a) a span that would round past UINT64_MAX when
 * the loader page-aligns vaddr_end (`+ VMM_PAGE_SIZE - 1`), which would blow up
 * the mapping page count, and (b) a kernel-half / non-canonical vaddr, which
 * would otherwise install USER-flagged PTEs over the kernel-half page tables
 * (vmm_map_page does not itself range-check the virtual address). `user_top`
 * is the highest mappable user virtual address (VMM_USER_TOP). */
static inline int elf_vaddr_in_user_range(uint64_t vaddr, uint64_t memsz,
                                          uint64_t user_top) {
  if (vaddr > user_top) return 0;
  if (memsz > user_top - vaddr) return 0;
  return 1;
}

#endif /* KERNEL_ELF_BOUNDS_H */
