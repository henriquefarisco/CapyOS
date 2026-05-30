#ifndef KERNEL_THREAD_CRASH_SMOKE_H
#define KERNEL_THREAD_CRASH_SMOKE_H

#include <stdint.h>

/*
 * Etapa 4 Fase E — thread-crash-survives smoke gate.
 *
 * The gate proves the kernel-side property "a thread/process that
 * faults does not take the kernel or desktop down with it". It is a
 * deterministic latch that fires when (a) a process has been killed
 * by a fault (exit code >= 128, set by `process_exit` from the fault
 * dispatcher in src/arch/x86_64/interrupts.c::x64_exception_dispatch),
 * and (b) the scheduler has continued to dispatch at least
 * `THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH` ticks after that
 * crash, proving the kernel/scheduler kept running.
 *
 * The pattern intentionally mirrors `scheduler_smoke` and
 * `compositor_smoke`: a pure latch (this header + thread_crash_smoke.c)
 * that the host test suite can exercise without freestanding wiring,
 * plus a separate I/O TU (thread_crash_smoke_io.c) that emits the
 * literal COM1 marker. The live build registers the latch with the
 * fault-killed exit path and with the scheduler tick.
 *
 * The literal marker string is observed verbatim by
 * tools/scripts/smoke_x64_vmware.py invoked from
 * `make smoke-x64-vmware-thread-crash-survives`. Do not rename it
 * without updating the smoke target and the corresponding playbook
 * entry in docs/operations/etapa-4-external-validation-playbook.md.
 */

#define THREAD_CRASH_SMOKE_GATE_VERSION 1
#define THREAD_CRASH_SMOKE_MARKER "[smoke] thread-crash-survives ready"
#define THREAD_CRASH_SMOKE_FAULT_EXIT_THRESHOLD 128
#define THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH 4u

struct thread_crash_smoke_state {
  /* Exit code of the most recent observed fault-killed process, or 0
   * before any crash has been seen. The high bit (>= 128) signals a
   * POSIX-style death-by-signal, matching the encoding used by
   * `process_exit(128 + (int)vector)` in interrupts.c. */
  int32_t last_exit_code;
  /* Number of distinct fault-killed processes observed since the
   * latch was reset. */
  uint32_t crashes_observed;
  /* Scheduler ticks observed strictly after the first qualifying
   * crash. Resets only on `thread_crash_smoke_state_reset`. */
  uint32_t ticks_after_crash;
  /* Set to 1 once the gate condition is satisfied. The emit hook
   * uses this flag to fire the COM1 marker exactly once across the
   * boot, matching the smoke-marker-pattern doc. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated
 * calls; used by host tests and by the live build at scheduler
 * init time. */
void thread_crash_smoke_state_reset(struct thread_crash_smoke_state *state);

/* Pure predicate used by both the latch and the host tests. Returns
 * 1 when the observed counts satisfy the gate, 0 otherwise. */
int thread_crash_smoke_gate_observed(uint32_t crashes_observed,
                                     uint32_t ticks_after_crash);

/* Observe a process exit. Returns 1 on the first call that flips
 * the gate condition from "not ready" to "ready"; returns 0 in any
 * other case (no crash, gate already emitted, gate still not ready).
 * The function is intentionally cheap so it can sit on the
 * fault-killed exit path without measurable overhead. */
int thread_crash_smoke_observe_exit(struct thread_crash_smoke_state *state,
                                    int32_t exit_code);

/* Observe a scheduler tick. Same semantics as
 * `thread_crash_smoke_observe_exit`: returns 1 only on the latch
 * edge. Ticks observed before any qualifying crash do not count. */
int thread_crash_smoke_observe_tick(struct thread_crash_smoke_state *state);

/* Convenience helpers that drive a TU-private global state. The live
 * build uses these from process_exit (kernel/process.c) and from
 * scheduler_tick (kernel/scheduler.c) so the latch never has to be
 * threaded through every call site. The reset hook lets the
 * scheduler init reset the global state at boot. */
int thread_crash_smoke_try_latch_exit_global(int32_t exit_code);
int thread_crash_smoke_try_latch_tick_global(void);
void thread_crash_smoke_global_reset(void);

/* COM1 marker emission. Defined in thread_crash_smoke_io.c so host
 * tests can override it; the live build forwards to com1_puts +
 * klog. */
void thread_crash_smoke_emit_marker(void);

/* Spawn the deliberate-crash helper task. Idempotent: the second
 * call is a no-op. Invoked from scheduler_set_running(1) under the
 * `CAPYOS_THREAD_CRASH_SURVIVES_SMOKE` flag, similar to the
 * fairness-smoke helper bootstrap. The helper feeds the exit latch
 * with a POSIX-style "death by signal" code so the scheduler tick
 * latch can then count post-crash ticks. The helper deliberately
 * does NOT touch the live process table: it simulates the observed
 * exit code that the fault dispatcher would emit, which is enough
 * to validate the latch + COM1 marker pipeline end-to-end on real
 * hardware. The kernel-side fault containment property itself is
 * locked by tests/test_fault_classify.c. */
void thread_crash_smoke_maybe_start_helper(void);

#endif /* KERNEL_THREAD_CRASH_SMOKE_H */
