#include "kernel/elf_bounds.h"

#include <stdio.h>

/*
 * Locks the overflow-safe ELF bounds predicates. Each "reject" case uses an
 * attacker-style value near UINT64_MAX that a naive `a + b <= size` check
 * would let wrap past the bound (the historical bug class fixed in
 * elf_loader.c). These must reject; valid in-range cases must accept.
 */
int run_elf_bounds_tests(void) {
  int fails = 0;
  const uint64_t PHDR = 56u; /* sizeof(struct elf64_phdr) */
  const uint64_t MAXU = ~(uint64_t)0;

  /* elf_phdr_entry_fits: accept valid entries (incl. exact fit). */
  if (!elf_phdr_entry_fits(64u, 0u, PHDR, 4096u) ||
      !elf_phdr_entry_fits(64u, 5u * PHDR, PHDR, 4096u) ||
      !elf_phdr_entry_fits(64u, 4096u - 64u - PHDR, PHDR, 4096u)) {
    printf("[elf-bounds] valid phdr entry rejected\n");
    fails++;
  }
  /* elf_phdr_entry_fits: reject overflow + out-of-range. */
  if (elf_phdr_entry_fits(MAXU - 16u, 0u, PHDR, 4096u) /* e_phoff wrap */ ||
      elf_phdr_entry_fits(4096u, 0u, PHDR, 4096u)      /* no room at end */ ||
      elf_phdr_entry_fits(64u, 5000u, PHDR, 4096u)     /* entry past end */ ||
      elf_phdr_entry_fits(64u, 4096u - 64u - PHDR + 1u, PHDR, 4096u)) {
    printf("[elf-bounds] out-of-range phdr entry accepted\n");
    fails++;
  }

  /* elf_range_in_bounds: accept valid ranges (incl. empty + exact fit). */
  if (!elf_range_in_bounds(100u, 200u, 4096u) ||
      !elf_range_in_bounds(4096u, 0u, 4096u) ||
      !elf_range_in_bounds(4000u, 96u, 4096u)) {
    printf("[elf-bounds] valid segment range rejected\n");
    fails++;
  }
  /* elf_range_in_bounds: reject overflow + overrun. */
  if (elf_range_in_bounds(MAXU - 0x100u, 0x200u, 4096u) /* p_offset wrap */ ||
      elf_range_in_bounds(4000u, 200u, 4096u)           /* span overruns */ ||
      elf_range_in_bounds(5000u, 0u, 4096u)             /* offset past end */) {
    printf("[elf-bounds] out-of-range segment accepted\n");
    fails++;
  }

  /* elf_sum_no_wrap: accept non-wrapping (incl. sum == UINT64_MAX). */
  if (!elf_sum_no_wrap(0x1000u, 0x2000u) ||
      !elf_sum_no_wrap(0x100u, MAXU - 0x100u) ||
      !elf_sum_no_wrap(12345u, 0u)) {
    printf("[elf-bounds] non-wrapping sum rejected\n");
    fails++;
  }
  /* elf_sum_no_wrap: reject wrapping. */
  if (elf_sum_no_wrap(MAXU - 0x100u, 0x200u) ||
      elf_sum_no_wrap(0x101u, MAXU - 0x100u)) {
    printf("[elf-bounds] wrapping sum accepted\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] elf_bounds OK\n");
  }
  return fails;
}
