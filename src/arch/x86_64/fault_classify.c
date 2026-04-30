#include "arch/x86_64/fault_classify.h"

#include <stddef.h>

/* Vectors that indicate hardware/platform corruption and must never be
 * contained inside a process kill, regardless of which CPL faulted. */
#define VEC_NMI 2u
#define VEC_DOUBLE_FAULT 8u
#define VEC_PAGE_FAULT 14u
#define VEC_MACHINE_CHECK 18u

/* Page-fault error code bits (Intel SDM Vol 3 Sec 4.7).
 *
 * The classifier only inspects the bits that influence the panic /
 * kill / recoverable decision. The complete bit list is documented
 * here so the next phase (real demand paging in `vmm_handle_page_fault`)
 * can extend the rules without re-deriving them.
 *
 *   bit 0  P    : 0 = page not present, 1 = protection violation
 *   bit 1  W    : 0 = read access,      1 = write access
 *   bit 2  U    : 0 = supervisor mode,  1 = user mode (matches CPL)
 *   bit 3  RSVD : 1 = reserved bit set in some paging-structure entry
 *   bit 4  I    : 1 = instruction fetch (relevant when NX is enabled)
 *   bit 5  PK   : 1 = protection-key violation (PKRU)
 *   bit 6  SS   : 1 = shadow-stack access (CET)
 *   bit 15 SGX  : 1 = SGX-specific access
 */
#define PF_ERR_PRESENT     (1u << 0)
#define PF_ERR_USER        (1u << 2)
#define PF_ERR_RESERVED    (1u << 3)
#define PF_ERR_PROTECTKEY  (1u << 5)

int arch_fault_is_user(uint64_t cs) {
  /* RPL is the bottom two bits of any segment selector. CPL == 3 means
   * the fault was taken while running user code; the CPU saves the
   * outer ring's CS in the IRET frame. */
  return (int)((cs & 0x3u) == 0x3u);
}

static int vector_is_user_recoverable(uint64_t vector) {
  /* Vectors a user program can legitimately trigger and that we are
   * willing to convert into a process kill. The list intentionally
   * excludes platform-fatal vectors (NMI, #DF, #MC) and reserved
   * vectors. Unknown vectors fall through to panic so that genuine
   * kernel bugs in the IDT setup show up loud. */
  switch (vector) {
    case 0:  /* #DE Divide Error */
    case 1:  /* #DB Debug */
    case 3:  /* #BP Breakpoint */
    case 4:  /* #OF Overflow */
    case 5:  /* #BR Bound Range */
    case 6:  /* #UD Invalid Opcode */
    case 7:  /* #NM Device Not Available */
    case 10: /* #TS Invalid TSS */
    case 11: /* #NP Segment Not Present */
    case 12: /* #SS Stack-Segment Fault */
    case 13: /* #GP General Protection */
    case 14: /* #PF Page Fault */
    case 16: /* #MF x87 FP Error */
    case 17: /* #AC Alignment Check */
    case 19: /* #XM SIMD FP Error */
    case 20: /* #VE Virtualization */
    case 21: /* #CP Control Protection */
      return 1;
    default:
      return 0;
  }
}

/* Phase 7a (M4 finalization): is this user-mode #PF a candidate for the
 * VMM recovery path? The CPL check is performed by the caller; this
 * helper only decides based on vector + error code.
 *
 * A page fault is "potentially recoverable" when:
 *   - vector == 14 (#PF), AND
 *   - the page was not present (bit 0 P clear), AND
 *   - no reserved-bit corruption (bit 3 clear), AND
 *   - no protection-key violation (bit 5 clear).
 *
 * Anything else (P=1 protection violation, RSVD=1 page-table corruption,
 * PK=1 PKRU violation) is rejected here and falls through to the
 * existing KILL_PROCESS path so a misbehaving user program is contained
 * even when `vmm_handle_page_fault` does nothing yet.
 *
 * Future phases extend this rule (e.g. P=1 with W=1 on a CoW-marked
 * page becomes recoverable when copy-on-write lands). The host tests
 * in `tests/test_fault_classify.c` lock the current matrix so those
 * extensions are explicit. */
static int pf_error_code_is_recoverable(uint64_t error_code) {
  if (error_code & PF_ERR_PRESENT) return 0;
  if (error_code & PF_ERR_RESERVED) return 0;
  if (error_code & PF_ERR_PROTECTKEY) return 0;
  return 1;
}

enum arch_fault_action
arch_fault_classify(const struct arch_fault_info *info) {
  if (info == NULL) {
    return ARCH_FAULT_KERNEL_PANIC;
  }

  /* Platform-fatal vectors panic regardless of CPL. They indicate
   * hardware-level corruption that cannot be safely contained. */
  if (info->vector == VEC_NMI || info->vector == VEC_DOUBLE_FAULT ||
      info->vector == VEC_MACHINE_CHECK) {
    return ARCH_FAULT_KERNEL_PANIC;
  }

  /* Kernel-mode fault: always fatal. Bug in kernel code or driver.
   * Even kernel-mode #PF panics: the kernel never demand-pages its
   * own working set today, so a fault here means a real bug (stale
   * pointer, unmapped MMIO, etc.). */
  if (!arch_fault_is_user(info->cs)) {
    return ARCH_FAULT_KERNEL_PANIC;
  }

  /* User-mode fault, but vector is not one we model as user-recoverable
   * (e.g. unknown / reserved vectors 9, 15, 22..27, 30..31). Fall back
   * to panic so the bug is visible. */
  if (!vector_is_user_recoverable(info->vector)) {
    return ARCH_FAULT_KERNEL_PANIC;
  }

  /* Phase 7a: a user-mode #PF on a not-present page is a candidate for
   * the VMM recovery path (demand paging, lazy mmap, future CoW). The
   * dispatcher consults `vmm_handle_page_fault` next; if that hook
   * returns 0, the user instruction resumes. If it returns non-zero
   * the dispatcher escalates to the existing KILL_PROCESS path. */
  if (info->vector == VEC_PAGE_FAULT &&
      pf_error_code_is_recoverable(info->error_code)) {
    return ARCH_FAULT_RECOVERABLE;
  }

  return ARCH_FAULT_KILL_PROCESS;
}
