#ifndef KERNEL_CAPYGFX_SMOKE_H
#define KERNEL_CAPYGFX_SMOKE_H

#include <stdint.h>

/*
 * Etapa 7 / Slice 7.2.2 — capygfx smoke gate.
 *
 * Proves the ring-3 graphical surface ABI end-to-end on real boot. The ring-3
 * `/bin/capygfx` program (userland/bin/capygfx/main.c) creates a compositor
 * window via SYS_WINDOW_CREATE, fills it, rasterizes a small display list into
 * an ARGB32 buffer and blits it via SYS_SURFACE_BLIT, presents it, polls input
 * once, and `capy_exit(0)` only if every graphical syscall succeeded; any
 * failure exits non-zero. This latch observes that success exit and emits the
 * COM1 marker exactly once.
 *
 * Why the exit code and not the program's stdout: ring-3 `capy_write(1, ...)`
 * lands on the debug-console (port 0xE9), which the QEMU smokes capture but
 * VMware does NOT. The VMware harness reads COM1, so the authoritative marker
 * must be emitted kernel-side — the same mechanism the tls-handshake and
 * capybrowse-text gates use.
 *
 * Mirrors capybrowse_text_smoke: a pure latch (this header + capygfx_smoke.c)
 * the host tests exercise without freestanding wiring, plus a separate I/O TU
 * (capygfx_smoke_io.c) that emits the literal COM1 marker. The live build feeds
 * the latch from process_exit under CAPYOS_GFX_SMOKE.
 *
 * The marker string is observed verbatim by the QEMU/VMware smoke harness via
 * `make smoke-x64-qemu-capygfx` / `make smoke-x64-vmware-browser-graphical`. Do
 * not rename it without updating the smoke target and docs.
 */

#define CAPYGFX_SMOKE_GATE_VERSION 1
#define CAPYGFX_SMOKE_MARKER "[smoke] capygfx ready"
/* The capygfx program exits 0 only after every graphical syscall succeeded; any
 * failure (create / fill / blit / present) exits non-zero. */
#define CAPYGFX_SMOKE_SUCCESS_EXIT_CODE 0

struct capygfx_smoke_state {
  /* Count of qualifying success exits (code == 0) observed since reset. */
  uint32_t successes_observed;
  /* Set to 1 once the marker has fired, so emission is exactly-once. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated calls. */
void capygfx_smoke_state_reset(struct capygfx_smoke_state *state);

/* Pure predicate shared by the latch and host tests: 1 when the observed
 * success count satisfies the gate (>= 1), 0 otherwise. */
int capygfx_smoke_gate_observed(uint32_t successes_observed);

/* Observe a process exit. Returns 1 on the single call that flips the gate from
 * "not ready" to "ready" (first code == 0 exit); 0 otherwise (non-zero exit,
 * already emitted, NULL state). Cheap enough for the process_exit path. */
int capygfx_smoke_observe_exit(struct capygfx_smoke_state *state,
                               int32_t exit_code);

/* Process-wide single-emission latch. The live build drives this from
 * process_exit (kernel/process.c); the reset hook lets boot reset it. */
int capygfx_smoke_try_latch_exit_global(int32_t exit_code);
void capygfx_smoke_global_reset(void);

/* COM1 marker emission. Defined in capygfx_smoke_io.c so host tests can override
 * it; the live build forwards to com1_puts + klog. */
void capygfx_smoke_emit_marker(void);

#endif /* KERNEL_CAPYGFX_SMOKE_H */
