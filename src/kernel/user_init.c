/*
 * Glue between the embedded `hello.elf` blob and the existing
 * process / ELF loader machinery. Phase 5c builds the helper but
 * does NOT call it from `kernel_main`; phase 5d will wire the boot
 * flow and add a QEMU smoke that asserts "hello, capyland" reaches
 * the debug console.
 *
 * The control flow is intentionally linear so the host test
 * (tests/test_user_init.c) can lock each branch via stubs.
 *
 *  1. Read the (data, size) pair from `embedded_hello_*`.
 *  2. Pre-validate via `elf_validate` so a corrupted blob fails
 *     with `KERNEL_SPAWN_BAD_ELF` before we burn a process slot.
 *  3. `process_create("hello", 0, 0)` allocates the address space.
 *  4. `elf_load_into_process` maps the segments and primes the
 *     main thread's RIP/RSP. On any failure we return
 *     `KERNEL_SPAWN_LOAD_FAILED`; phase 6 will reclaim the slot.
 *  5. The caller decides when to call `process_enter_user_mode`.
 */
#include "kernel/user_init.h"
#include "kernel/process.h"
#include "kernel/elf_loader.h"
#include "kernel/embedded_hello.h"
#include "kernel/embedded_progs.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/user_task_init.h"
#include "kernel/arch_sched_hooks.h"

int kernel_spawn_embedded_hello(struct process **out_proc) {
  const uint8_t *data = (const uint8_t *)embedded_hello_data();
  size_t size = embedded_hello_size();

  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("hello", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    /* M4 phase 6 closes the embryo-slot leak documented in the
     * original phase 5c TODO: tear down the half-built process so
     * the table slot, address space, and (eventual) main thread
     * are reclaimed before we return to the boot path. */
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  if (out_proc) *out_proc = p;
  return KERNEL_SPAWN_OK;
}

int kernel_boot_run_embedded_hello(void) {
  struct process *p = NULL;
  int rc = kernel_spawn_embedded_hello(&p);
  if (rc != KERNEL_SPAWN_OK || !p) {
    /* Caller logs and falls through to the kernel shell. */
    return rc;
  }
  /* `process_enter_user_mode` is __attribute__((noreturn)) on the
   * success path: it iretq's into Ring 3 and execution does not
   * come back to this function. The defensive `return -1` below
   * exists only so the compiler does not assume a fall-through
   * path when the implementation is stubbed out under UNIT_TEST or
   * if a future refactor accidentally drops the noreturn. */
  process_enter_user_mode(p);
  return -1;
}

/* M4 phase 8f.5: two-task ring-3 preemption helper.
 *
 * 1. Spawn two copies of the embedded hello binary via the existing
 *    kernel_spawn_embedded_hello path (each gets its own AS, main
 *    thread, kernel stack, user stack, primed RIP/RSP).
 * 2. Arm process_b's main thread via the synthetic IRET frame
 *    builder with rank=1 so context_switch can dispatch it via
 *    `x64_user_first_dispatch`.
 * 3. Add process_b's main thread to the scheduler run queue.
 * 4. Set process_a's main thread as task_current() and refresh the
 *    arch RSP0/cpu_local.kernel_rsp to point at process_a's per-task
 *    kernel stack (this is what 8f.2 wires for kernel-mode tasks;
 *    here we manually invoke the seam before the iretq).
 * 5. iretq into process_a via process_enter_user_mode (which uses
 *    the existing direct iretq path with rank=0 implicitly because
 *    the boot iretq site does not touch RAX). When the first
 *    quantum exhausts, scheduler picks process_b and dispatches it
 *    via the synthetic IRET trampoline.
 *
 * Failure cleanup: any partial state is destroyed before return. */
int kernel_boot_run_two_busy_users(void) {
  struct process *pa = NULL;
  struct process *pb = NULL;

  int rc = kernel_spawn_embedded_hello(&pa);
  if (rc != KERNEL_SPAWN_OK || !pa) return rc;

  rc = kernel_spawn_embedded_hello(&pb);
  if (rc != KERNEL_SPAWN_OK || !pb) {
    process_destroy(pa);
    return rc;
  }

  /* Force the second copy's main thread name so the scheduler logs
   * (when they exist) can tell them apart. The kernel never reads
   * the name; this is purely for human inspection. */
  if (pb->main_thread) {
    /* Best-effort: just stomp the first character. The full
     * task_name_copy helper is private to task.c. */
    pb->main_thread->name[0] = 'B';
  }

  /* Arm pb's main thread via the synth IRET frame builder so the
   * scheduler can dispatch it on quantum exhaustion. The user RIP
   * and RSP are exactly what elf_load_into_process primed into
   * pb->main_thread->context. */
  if (!pb->main_thread || !pb->main_thread->kernel_stack) {
    process_destroy(pa);
    process_destroy(pb);
    return KERNEL_SPAWN_LOAD_FAILED;
  }
  uint64_t pb_rip = pb->main_thread->context.rip;
  uint64_t pb_rsp = pb->main_thread->context.rsp;
  user_task_arm_for_first_dispatch_with_rax(pb->main_thread, pb_rip,
                                            pb_rsp, 1u);

  /* Add pb to the run queue so scheduler_pick_next will find it
   * once pa's quantum runs out. pa is NOT added: it is the
   * "current" task by virtue of the iretq below, and adding it to
   * the queue would let pick_next return it twice. */
  scheduler_add(pb->main_thread);

  /* Mark pa as the running task so the first APIC tick from ring 3
   * sees a coherent task_current(). Refresh cpu_local + TSS RSP0 to
   * pa's per-task kernel stack so the IRET frame the CPU pushes on
   * tick lands on the right stack. */
  if (pa->main_thread) {
    extern void task_set_current(struct task *t);
    pa->main_thread->state = TASK_STATE_RUNNING;
    task_set_current(pa->main_thread);
    arch_sched_apply_kernel_stack(pa->main_thread);
  }

  /* Drop into ring 3 for pa. process_enter_user_mode is noreturn on
   * success (existing iretq path). When the scheduler later swaps
   * to pb on quantum exhaustion, context_switch will land on
   * x64_user_first_dispatch which iretqs into pb at rank=1. */
  process_enter_user_mode(pa);
  return -1;
}

/* M5 phase E.5: boot directly into the embedded capysh interactive
 * shell. Same control-flow shape as `kernel_boot_run_embedded_hello`
 * but resolves the binary through the embedded_progs registry
 * instead of the legacy `embedded_hello_data` accessor. */
int kernel_boot_run_capysh(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup("/bin/capysh", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("capysh", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* `process_enter_user_mode` is noreturn on success. */
  process_enter_user_mode(p);
  return -1;
}
