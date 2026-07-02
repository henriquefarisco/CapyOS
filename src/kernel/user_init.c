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
#if defined(CAPYOS_GFX_SMOKE) || defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER)
#include "kernel/syscall_gfx.h"
#include "drivers/io.h"
#endif

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

#ifdef CAPYOS_TLS_HANDSHAKE_SMOKE
/* Etapa 5 / Slice 5.6: boot directly into the embedded tls_smoke program,
 * the userland TLS handshake gate. Same control-flow shape as
 * kernel_boot_run_capysh, resolving /bin/tls_smoke through the
 * embedded_progs registry. Compiled only under the smoke gate (the blob
 * exists only then). The program retries its HTTPS GET until the async
 * DHCP lease lands, so it tolerates running before the network is up. */
int kernel_boot_run_tls_smoke(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup("/bin/tls_smoke", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("tls_smoke", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* `process_enter_user_mode` is noreturn on success. */
  process_enter_user_mode(p);
  return -1;
}
#endif

#ifdef CAPYOS_CAPYBROWSE_SMOKE
/* Etapa 6 / Slice 6.4: boot directly into the embedded capybrowse program,
 * the CapyBrowse Text gate. Same control-flow shape as
 * kernel_boot_run_tls_smoke, resolving /bin/capybrowse through the
 * embedded_progs registry. Compiled only under the smoke gate (the blob exists
 * only then). The program retries its HTTPS GET until the async DHCP lease
 * lands, so it tolerates running before the network is up. */
int kernel_boot_run_capybrowse(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup("/bin/capybrowse", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("capybrowse", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* `process_enter_user_mode` is noreturn on success. */
  process_enter_user_mode(p);
  return -1;
}
#endif

#ifdef CAPYOS_MULTIFETCH_SMOKE
/* Etapa 7 / Slice 7.5: boot directly into the embedded capymultifetch
 * program, the browser-multifetch smoke gate. Same control-flow shape as
 * kernel_boot_run_capybrowse, resolving /bin/capymultifetch through the
 * embedded_progs registry. Compiled only under the smoke gate (the blob
 * exists only then). The program fetches its configured URL twice through a
 * persistent browser_fetch_ctx and exits 0 iff the 2nd visit was served from
 * the cache without a 2nd network transport call. */
int kernel_boot_run_capymultifetch(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup("/bin/capymultifetch", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("capymultifetch", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* `process_enter_user_mode` is noreturn on success. */
  process_enter_user_mode(p);
  return -1;
}
#endif

#ifdef CAPYOS_GFX_SMOKE
/* Etapa 7 / Slice 7.2.2: boot directly into the embedded capygfx program, the
 * ring-3 graphical surface gate. Same control-flow shape as
 * kernel_boot_run_capybrowse, resolving /bin/capygfx through the embedded_progs
 * registry. Compiled only under the smoke gate. The caller (kernel_main) must
 * have already initialised the compositor over the boot framebuffer and
 * installed the graphical-syscall backend (syscall_gfx_install_default_ops);
 * the program then creates a window, fills it, rasterizes a display list and
 * blits it, presents, and exits 0 -- which process_exit latches into the COM1
 * marker. */
int kernel_boot_run_capygfx(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup("/bin/capygfx", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("capygfx", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* `process_enter_user_mode` is noreturn on success. */
  process_enter_user_mode(p);
  return -1;
}
#endif

#if defined(CAPYOS_GFX_SMOKE) || defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER)
/* Etapa 7 / Slice 7.5 (alpha.304): spawn /bin/capygfx as an ORDINARY process
 * from a caller that keeps running (see the doc comment on the declaration in
 * include/kernel/user_init.h for the full rationale). Mirrors the "second
 * process" half of kernel_boot_run_two_busy_users (arm for first dispatch +
 * scheduler_add) but never calls process_enter_user_mode.
 *
 * Etapa 7 / Slice 7.5 (alpha.306) -- CRITICAL FIX after a real VMware crash:
 * process_create's process-table scan,
 * elf_load_into_process's page-table/physical-memory mutations and
 * scheduler_add's run-queue insertion have NO internal locking anywhere in
 * this kernel -- every OTHER caller of this same sequence
 * (kernel_boot_run_embedded_hello/_two_busy_users/_capysh/_tls_handshake/
 * _capybrowse/_capymultifetch/_capygfx, all in this file) runs at boot,
 * before the preemptive timer IRQ is armed, so the sequence is atomic BY
 * CONSTRUCTION (nothing else can run concurrently yet). sys_fork
 * (src/kernel/syscall.c) reaches the SAME process_create from a syscall
 * trap gate, which masks IF for the duration on this arch. This function is
 * the FIRST caller of the sequence that runs from a live desktop session
 * with the preemptive scheduler already ticking and interrupts enabled (a
 * shell command handler is plain kernel-mode C, not a syscall gate) --
 * without protection, a timer IRQ landing mid-sequence can hand the
 * partially-initialised process table / page tables / run queue to whatever
 * the scheduler switches to next, corrupting shared state. Bracket the
 * mutating span with cli()/sti() (save/restore, in case a future caller
 * already runs with interrupts disabled) to make it atomic here too. */
static inline uint64_t kernel_spawn_save_flags(void) {
  uint64_t flags;
  __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
  return flags;
}
static inline void kernel_spawn_restore_flags(uint64_t flags) {
  __asm__ __volatile__("pushq %0; popfq" : : "r"(flags) : "memory");
}

int kernel_spawn_capygfx_desktop(void) {
  const uint8_t *data = NULL;
  size_t size = 0;
  uint64_t irq_flags;

  /* Idempotent: installing the same ops vtable + teardown observer twice is a
   * harmless no-op (see syscall_gfx_install_default_ops). The caller is, by
   * construction, already inside a live desktop session, so the compositor is
   * already initialised over the real framebuffer. */
  syscall_gfx_install_default_ops();

  if (embedded_progs_lookup("/bin/capygfx", &data, &size) != 0) {
    return KERNEL_SPAWN_BAD_ELF;
  }
  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  irq_flags = kernel_spawn_save_flags();
  cli();

  struct process *p = process_create("capygfx", 0, 0);
  if (!p) {
    kernel_spawn_restore_flags(irq_flags);
    return KERNEL_SPAWN_NO_PROCESS;
  }

  if (elf_load_into_process(p, data, size) != 0) {
    process_destroy(p);
    kernel_spawn_restore_flags(irq_flags);
    return KERNEL_SPAWN_LOAD_FAILED;
  }
  if (!p->main_thread || !p->main_thread->kernel_stack) {
    process_destroy(p);
    kernel_spawn_restore_flags(irq_flags);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  /* Arm the main thread via the synthetic IRET frame builder: elf_load
   * already primed context.rip/rsp with the user entry point + stack, so we
   * hand those straight to the builder (same pattern as pb in
   * kernel_boot_run_two_busy_users) and queue it for the scheduler to pick up
   * on the next (voluntary or preemptive) context switch. */
  {
    uint64_t rip = p->main_thread->context.rip;
    uint64_t rsp = p->main_thread->context.rsp;
    user_task_arm_for_first_dispatch_with_rax(p->main_thread, rip, rsp, 1u);
  }
  scheduler_add(p->main_thread);
  kernel_spawn_restore_flags(irq_flags);
  return KERNEL_SPAWN_OK;
}
#endif

#ifdef CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE
/* Etapa 7 / Slice 7.5 (alpha.304): see the doc comment on the declaration in
 * include/kernel/user_init.h. Exact shape of kernel_boot_run_two_busy_users
 * (pa = hello, entered directly; pb = capygfx here, via
 * kernel_spawn_capygfx_desktop instead of a second hello copy). */
int kernel_boot_run_capygfx_desktop_spawn_smoke(void) {
  struct process *pa = NULL;

  int rc = kernel_spawn_embedded_hello(&pa);
  if (rc != KERNEL_SPAWN_OK || !pa) return rc;

  rc = kernel_spawn_capygfx_desktop();
  if (rc != KERNEL_SPAWN_OK) {
    process_destroy(pa);
    return rc;
  }

  /* Mark pa as the running task so the first APIC tick / voluntary yield from
   * ring 3 sees a coherent task_current(). Refresh cpu_local + TSS RSP0 to
   * pa's per-task kernel stack (same as kernel_boot_run_two_busy_users). */
  if (pa->main_thread) {
    extern void task_set_current(struct task *t);
    pa->main_thread->state = TASK_STATE_RUNNING;
    task_set_current(pa->main_thread);
    arch_sched_apply_kernel_stack(pa->main_thread);
  }

  /* `process_enter_user_mode` is noreturn on success. When pa exits (task_exit
   * internally yields), the scheduler picks capygfx (pb) off the run queue and
   * dispatches it via the synthetic IRET trampoline armed above. */
  process_enter_user_mode(pa);
  return -1;
}
#endif

#ifdef CAPYOS_APPS_ROUNDTRIP_SMOKE
#include "apps/apps_smoke.h"
#include "kernel/apps_roundtrip_smoke.h"
#include <stdint.h>

/* Etapa 6 / Slice 6.6: in-kernel apps-basic-roundtrip orchestrator.
 *
 * Unlike the capybrowse/tls smokes (ring-3 ELFs that exit), the basic desktop
 * apps are in-kernel functions compiled into the kernel ELF (CapyUI desktop
 * session), so there is no process to spawn or exit. This orchestrator runs
 * each app's headless primary-function roundtrip via the apps/apps_smoke.h
 * contract (implemented by CapyUI) and feeds each 0/non-0 result to the
 * apps_roundtrip_smoke latch, which emits `[smoke] apps-basic-roundtrip ready`
 * on COM1 once APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS clean passes are observed. A
 * failure (non-0) is not counted, so the gate times out (fail) instead of
 * reporting ready. Returns after running; the caller falls through to login. */
int kernel_boot_run_apps_roundtrip(void) {
  unsigned total = apps_smoke_roundtrip_total();
  unsigned i;
  /* The latch threshold (APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS, compile-time) must
   * equal the app-set size CapyUI reports, or the count-to-N marker would
   * mis-fire: if REQUIRED < total the latch emits "ready" before the last apps
   * run (a later failure could no longer retract it -> false pass); if
   * REQUIRED > total it can never fire. Guard the drift so a mismatch fails the
   * gate (no marker -> times out) instead of producing a false pass. With them
   * equal, the latch emits iff every one of the `total` apps exits cleanly. */
  if (total != APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS) {
    return -1;
  }
  for (i = 0u; i < total; i++) {
    int rc = apps_smoke_roundtrip_run(i);
    if (apps_roundtrip_smoke_try_latch_exit_global((int32_t)rc)) {
      apps_roundtrip_smoke_emit_marker();
    }
  }
  return 0;
}
#endif
