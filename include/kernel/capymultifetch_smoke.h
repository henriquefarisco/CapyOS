#ifndef KERNEL_CAPYMULTIFETCH_SMOKE_H
#define KERNEL_CAPYMULTIFETCH_SMOKE_H

#include <stdint.h>

/*
 * Etapa 7 / Slice 7.5 — browser-multifetch smoke gate.
 *
 * Proves the persistent multi-fetch browser runtime (browser_fetch.c) on real
 * hardware. The ring-3 `/bin/capymultifetch` program (userland/bin/
 * capymultifetch/main.c) fetches a controlled URL over the real Etapa 5
 * userland TLS/socket transport TWICE through the SAME `browser_fetch_ctx`
 * (persistent cache + cookie jar + HSTS), then `capy_exit(0)` iff the second
 * visit was served from the cache with NO second network transport call
 * (`browser_fetch_ctx.transport_calls == 1`) — the runtime proof of the Etapa 7
 * "cache acelera a 2a visita" criterion; any other outcome exits non-zero.
 * This latch observes that success exit and emits the COM1 marker exactly
 * once.
 *
 * Why the exit code and not the program's stdout: ring-3 `capy_write(1, ...)`
 * lands on the debug-console (port 0xE9, see src/kernel/syscall.c::sys_write),
 * which the QEMU smokes capture but VMware does NOT. The VMware harness reads
 * COM1, so the authoritative marker must be emitted kernel-side — the same
 * mechanism the capybrowse-text / tls-handshake gates use.
 *
 * Mirrors capybrowse_text_smoke: a pure latch (this header + this file's
 * companion .c) the host tests exercise without freestanding wiring, plus a
 * separate I/O TU (capymultifetch_smoke_io.c) that emits the literal COM1
 * marker. The live build feeds the latch from process_exit under
 * CAPYOS_MULTIFETCH_SMOKE.
 *
 * The marker string is observed verbatim by
 * tools/scripts/smoke_x64_qemu_browser_multifetch.py /
 * tools/scripts/smoke_x64_vmware.py. Do not rename it without updating the
 * smoke scripts and docs.
 */

#define CAPYMULTIFETCH_SMOKE_GATE_VERSION 1
#define CAPYMULTIFETCH_SMOKE_MARKER "[smoke] browser-multifetch ready"
/* capymultifetch exits 0 only when the 2nd visit of the cacheable URL was
 * served from the cache with transport_calls == 1; any other outcome
 * (transport failure, cache short-circuit failure, budget exceeded) exits
 * non-zero. */
#define CAPYMULTIFETCH_SMOKE_SUCCESS_EXIT_CODE 0

struct capymultifetch_smoke_state {
  /* Count of qualifying success exits (code == 0) observed since reset. */
  uint32_t successes_observed;
  /* Set to 1 once the marker has fired, so emission is exactly-once. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated calls. */
void capymultifetch_smoke_state_reset(struct capymultifetch_smoke_state *state);

/* Pure predicate shared by the latch and host tests: 1 when the observed
 * success count satisfies the gate (>= 1), 0 otherwise. */
int capymultifetch_smoke_gate_observed(uint32_t successes_observed);

/* Observe a process exit. Returns 1 on the single call that flips the gate
 * from "not ready" to "ready" (first code == 0 exit); 0 otherwise (non-zero
 * exit, already emitted, NULL state). Cheap enough for the process_exit path. */
int capymultifetch_smoke_observe_exit(struct capymultifetch_smoke_state *state,
                                      int32_t exit_code);

/* Process-wide single-emission latch (smoke-marker-pattern Invariant #5). The
 * live build drives this from process_exit (kernel/process.c); the reset hook
 * lets boot reset the global state. */
int capymultifetch_smoke_try_latch_exit_global(int32_t exit_code);
void capymultifetch_smoke_global_reset(void);

/* COM1 marker emission. Defined in capymultifetch_smoke_io.c so host tests can
 * override it; the live build forwards to com1_puts + klog. */
void capymultifetch_smoke_emit_marker(void);

#endif /* KERNEL_CAPYMULTIFETCH_SMOKE_H */
