#ifndef ARCH_X86_64_FAULT_CLASSIFY_H
#define ARCH_X86_64_FAULT_CLASSIFY_H

/* Fault classification table for x86_64 CPU exceptions (vectors 0..31).
 *
 * The exception dispatcher in src/arch/x86_64/interrupts.c calls
 * arch_fault_classify() to decide whether a fault is a fatal kernel
 * event (panic + halt, current behaviour) or a user-mode fault that
 * should kill the offending process and let the scheduler keep going.
 *
 * The classification logic is intentionally side-effect free so that
 * tests/test_fault_classify.c can lock the full matrix on the host
 * without dragging in any kernel state. Production wiring lives in
 * x64_exception_dispatch.
 *
 * Phase 4 (M4 finalization) locked the panic-vs-kill contract. Phase
 * 7a now also returns ARCH_FAULT_RECOVERABLE for user-mode #PF on a
 * not-present page, so the dispatcher can route the fault through
 * `vmm_handle_page_fault` (demand paging, future CoW) before deciding
 * to kill the process. Phase 7b will extend the rule to cover P=1+W=1
 * on a CoW-marked page once copy-on-write lands. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum arch_fault_action {
  /* Fault must terminate the kernel: dump registers and halt. */
  ARCH_FAULT_KERNEL_PANIC = 0,
  /* Fault came from user mode: terminate the offending process and
   * reschedule. The dispatcher converts the vector into a POSIX-style
   * exit code via 128 + vector. */
  ARCH_FAULT_KILL_PROCESS = 1,
  /* User-mode #PF on a not-present page (M4 phase 7a). The dispatcher
   * must call `vmm_handle_page_fault(cr2, error_code)`: a return of 0
   * means the fault was handled and the user instruction resumes; a
   * non-zero return escalates the dispatcher to KILL_PROCESS. Future
   * recoverable cases (CoW, lazy mmap of file-backed pages) extend the
   * rule but keep this same return contract. */
  ARCH_FAULT_RECOVERABLE = 2,
};

struct arch_fault_info {
  uint64_t vector;     /* CPU exception vector, 0..31 */
  uint64_t error_code; /* CPU-supplied error code (0 if none) */
  uint64_t cs;         /* Saved CS at the moment of the fault */
  uint64_t rip;        /* Saved RIP (informational only) */
  uint64_t cr2;        /* Faulting linear address for #PF; 0 otherwise */
};

/* Classify a fault. Pure function: no globals, no side effects.
 *
 * Decision precedence (top to bottom; first match wins):
 *
 *   1. NULL info                                   -> KERNEL_PANIC
 *   2. NMI / #DF / #MC                             -> KERNEL_PANIC
 *      (platform-fatal regardless of saved CPL)
 *   3. Saved CS indicates kernel mode (CPL != 3)   -> KERNEL_PANIC
 *   4. User-mode vector outside the recoverable    -> KERNEL_PANIC
 *      table (e.g. reserved vectors 9, 15, 22..27, 30..31)
 *   5. User-mode #PF (vector 14) with error code   -> RECOVERABLE
 *      indicating not-present (P=0), no reserved-bit
 *      corruption (RSVD=0), and no protection-key
 *      violation (PK=0)
 *   6. Anything else from user mode                -> KILL_PROCESS
 *      (#DE, #UD, #NM, #TS, #NP, #SS, #GP, #PF with
 *       protection violation, #MF, #AC, #XM, #CP)
 *
 * Phase 7b extends rule (5) to also include user-mode write faults
 * (P=1, W=1) on pages marked CoW once copy-on-write lands. The host
 * tests in tests/test_fault_classify.c lock the current matrix so that
 * extension cannot silently regress existing decisions. */
enum arch_fault_action
arch_fault_classify(const struct arch_fault_info *info);

/* Returns 1 if the saved CS selector indicates user mode (RPL=3),
 * 0 otherwise. Equivalent to (cs & 0x3) == 3 but documented and tested
 * separately so that future selector-layout changes (e.g. adding ring 1
 * or ring 2 segments) cannot silently break the classifier. */
int arch_fault_is_user(uint64_t cs);

#ifdef __cplusplus
}
#endif

#endif /* ARCH_X86_64_FAULT_CLASSIFY_H */
