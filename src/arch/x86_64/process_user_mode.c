#include "arch/x86_64/cpu_local.h"
#include "kernel/process.h"
#include "kernel/task.h"
#include "memory/vmm.h"

#include <stdint.h>
#include <stddef.h>

/* x86_64 implementation of process_enter_user_mode().
 *
 * Two pieces of state must be in place before we drop into Ring 3:
 *
 * 1. cpu_local_init() must have run so the syscall path's
 *    `%gs:CPU_LOCAL_KERNEL_RSP_OFFSET` accesses reach a valid kernel
 *    stack pointer. We refuse to enter user mode otherwise; the
 *    failure is silent only because there is no current process to
 *    panic against - the caller must have driven cpu_local_init()
 *    early in `kernel_main`.
 *
 * 2. The process must have a populated main_thread with a non-zero
 *    rip and rsp in its context. ELF loading sets these via
 *    elf_load_from_file().
 *
 * The actual ring transition is in user_mode_entry.S; this file is
 * just the C-side validator and seam. */

extern void enter_user_mode(uint64_t rip, uint64_t rsp)
    __attribute__((noreturn));

int process_enter_user_mode(struct process *proc) {
  if (!proc) {
    return PROCESS_ENTER_USER_MODE_INVALID_PROC;
  }
  if (!proc->main_thread) {
    return PROCESS_ENTER_USER_MODE_NO_THREAD;
  }

  uint64_t rip = proc->main_thread->context.rip;
  uint64_t rsp = proc->main_thread->context.rsp;

  if (rip == 0) {
    return PROCESS_ENTER_USER_MODE_BAD_RIP;
  }
  if (rsp == 0) {
    return PROCESS_ENTER_USER_MODE_BAD_RSP;
  }

  /* Activate the process address space before the ring transition. This
   * path does not pass through the scheduler context switch, so CR3 would
   * otherwise stay on the kernel address space -- whose low region is a
   * supervisor huge identity map that does not contain the ELF-loaded user
   * image, making the ring-3 entry fetch fault (present, supervisor). The
   * process AS clones the kernel low half, so kernel code/stack and the
   * syscall %gs state stay mapped across the switch. */
  if (proc->address_space) {
    vmm_switch_address_space(proc->address_space);
  }

  /* On the success path enter_user_mode() never returns. The
   * compiler does not know that without the noreturn attribute on
   * the extern declaration above; with it, this function tail-calls
   * the asm helper. */
  enter_user_mode(rip, rsp);
}
