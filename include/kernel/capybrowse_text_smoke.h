#ifndef KERNEL_CAPYBROWSE_TEXT_SMOKE_H
#define KERNEL_CAPYBROWSE_TEXT_SMOKE_H

#include <stdint.h>

/*
 * Etapa 6 / Slice 6.4 — capybrowse-text smoke gate.
 *
 * Proves the CapyBrowse Text app end-to-end on real hardware. The ring-3
 * `/bin/capybrowse` program (userland/bin/capybrowse/main.c) fetches a
 * controlled URL over the Etapa 5 userland HTTPS/TLS path, runs the published
 * capy-browser-core HTML-to-text on the response, formats the page (title +
 * body + numbered links) and prints it, then `capy_exit(0)` on a successful
 * render; any failure exits non-zero. This latch observes that success exit and
 * emits the COM1 marker exactly once.
 *
 * Why the exit code and not the program's stdout: ring-3 `capy_write(1, ...)`
 * lands on the debug-console (port 0xE9, see src/kernel/syscall.c::sys_write),
 * which the QEMU smokes capture but VMware does NOT. The VMware harness reads
 * COM1, so the authoritative marker must be emitted kernel-side — the same
 * mechanism the tls-handshake gate uses (kernel/tls_handshake_smoke.h).
 *
 * Mirrors tls_handshake_smoke: a pure latch (this header + capybrowse_text_smoke.c)
 * the host tests exercise without freestanding wiring, plus a separate I/O TU
 * (capybrowse_text_smoke_io.c) that emits the literal COM1 marker. The live
 * build feeds the latch from process_exit under CAPYOS_CAPYBROWSE_SMOKE.
 *
 * The marker string is observed verbatim by tools/scripts/smoke_x64_vmware.py
 * via `make smoke-x64-vmware-capybrowse-text`. Do not rename it without
 * updating the smoke target and docs/architecture/etapa-6-desktop-apps-readiness.md.
 */

#define CAPYBROWSE_TEXT_SMOKE_GATE_VERSION 1
#define CAPYBROWSE_TEXT_SMOKE_MARKER "[smoke] capybrowse-text ready"
/* The capybrowse program exits 0 only on a successful fetch + HTML-to-text
 * render; any failure (transport, parse, or render) exits non-zero. */
#define CAPYBROWSE_TEXT_SMOKE_SUCCESS_EXIT_CODE 0

struct capybrowse_text_smoke_state {
  /* Count of qualifying success exits (code == 0) observed since reset. */
  uint32_t successes_observed;
  /* Set to 1 once the marker has fired, so emission is exactly-once. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated calls. */
void capybrowse_text_smoke_state_reset(
    struct capybrowse_text_smoke_state *state);

/* Pure predicate shared by the latch and host tests: 1 when the observed
 * success count satisfies the gate (>= 1), 0 otherwise. */
int capybrowse_text_smoke_gate_observed(uint32_t successes_observed);

/* Observe a process exit. Returns 1 on the single call that flips the gate
 * from "not ready" to "ready" (first code == 0 exit); 0 otherwise (non-zero
 * exit, already emitted, NULL state). Cheap enough for the process_exit path. */
int capybrowse_text_smoke_observe_exit(
    struct capybrowse_text_smoke_state *state, int32_t exit_code);

/* Process-wide single-emission latch (smoke-marker-pattern Invariant #5). The
 * live build drives this from process_exit (kernel/process.c); the reset hook
 * lets boot reset the global state. */
int capybrowse_text_smoke_try_latch_exit_global(int32_t exit_code);
void capybrowse_text_smoke_global_reset(void);

/* COM1 marker emission. Defined in capybrowse_text_smoke_io.c so host tests
 * can override it; the live build forwards to com1_puts + klog. */
void capybrowse_text_smoke_emit_marker(void);

#endif /* KERNEL_CAPYBROWSE_TEXT_SMOKE_H */
