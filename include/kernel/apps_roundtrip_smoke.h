#ifndef KERNEL_APPS_ROUNDTRIP_SMOKE_H
#define KERNEL_APPS_ROUNDTRIP_SMOKE_H

#include <stdint.h>

/*
 * Etapa 6 / Slice 6.6 — apps-basic-roundtrip smoke gate (pure latch).
 *
 * Proves the basic desktop apps survive an open/run/close roundtrip: each app
 * launches, runs its primary function, and exits cleanly (code 0) without
 * crashing the desktop. This latch observes process exits and emits the COM1
 * marker exactly once after APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS clean exits have
 * been seen. A non-clean exit (a crashed app: code != 0, e.g. a fault-killed
 * >= 128) is NOT counted and never fires the readiness marker, so a crash makes
 * the gate time out (fail) rather than report ready. Desktop *survival* of a
 * crash is the complementary, already-shipped thread-crash-survives gate
 * (include/kernel/thread_crash_smoke.h); this gate is about the clean roundtrip.
 *
 * Generalizes the single-exit latches (tls_handshake_smoke / capybrowse_text_smoke,
 * which are the REQUIRED == 1 case) to a count-to-threshold. The threshold is a
 * compile-time constant the boot-side orchestration sets to the number of apps
 * it launches; it is intentionally separate from this pure latch so the latch
 * stays host-testable with no knowledge of how apps are spawned.
 *
 * Pure: no allocation, no I/O, no kernel deps (mirrors thread_crash_smoke.c).
 * The COM1 marker emitter lives in apps_roundtrip_smoke_io.c so host tests can
 * override it; the live build feeds the global latch from process_exit under
 * CAPYOS_APPS_ROUNDTRIP_SMOKE. The marker string is observed verbatim by
 * tools/scripts/smoke_x64_vmware.py via `make smoke-x64-vmware-apps-basic-roundtrip`.
 */

#define APPS_ROUNDTRIP_SMOKE_GATE_VERSION 1
#define APPS_ROUNDTRIP_SMOKE_MARKER "[smoke] apps-basic-roundtrip ready"
#define APPS_ROUNDTRIP_SMOKE_CLEAN_EXIT_CODE 0

/* Number of clean app exits that satisfy the roundtrip. The boot-side
 * orchestration (which launches the apps) overrides this via -D to match the
 * app set it spawns; default 1 keeps the latch well-defined on its own. */
#ifndef APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS
#define APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS 1u
#endif

struct apps_roundtrip_smoke_state {
  /* Count of clean (code 0) process exits observed since reset. */
  uint32_t apps_completed;
  /* Set to 1 once the marker has fired, so emission is exactly-once. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated calls. */
void apps_roundtrip_smoke_state_reset(struct apps_roundtrip_smoke_state *state);

/* Pure predicate shared by the latch and host tests: 1 when the observed clean
 * count satisfies the gate (>= APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS), else 0. */
int apps_roundtrip_smoke_gate_observed(uint32_t apps_completed);

/* Observe a process exit. A clean exit (code 0) increments the count; a
 * non-clean exit is ignored. Returns 1 on the single call that flips the gate
 * from "not ready" to "ready" (the threshold-th clean exit); 0 otherwise
 * (non-clean exit, below threshold, already emitted, NULL state). */
int apps_roundtrip_smoke_observe_exit(struct apps_roundtrip_smoke_state *state,
                                      int32_t exit_code);

/* Process-wide single-emission latch (smoke-marker-pattern Invariant #5). The
 * live build drives this from process_exit (kernel/process.c); the reset hook
 * lets boot reset the global state. */
int apps_roundtrip_smoke_try_latch_exit_global(int32_t exit_code);
void apps_roundtrip_smoke_global_reset(void);

/* COM1 marker emission. Defined in apps_roundtrip_smoke_io.c so host tests can
 * override it; the live build forwards to com1_puts + klog. */
void apps_roundtrip_smoke_emit_marker(void);

#endif /* KERNEL_APPS_ROUNDTRIP_SMOKE_H */
