#ifndef KERNEL_TLS_HANDSHAKE_SMOKE_H
#define KERNEL_TLS_HANDSHAKE_SMOKE_H

#include <stdint.h>

/*
 * Etapa 5 / Slice 5.6 — tls-handshake smoke gate.
 *
 * Proves the userland TLS path end-to-end on real hardware. The ring-3
 * `/bin/tls_smoke` program (userland/bin/tls_smoke/main.c) performs a valid
 * HTTPS GET (must succeed) and an invalid-certificate HTTPS GET (must fail
 * closed) over libcapy-tls, then `capy_exit(0)` iff BOTH hold; any failure
 * exits non-zero. This latch observes that success exit and emits the COM1
 * marker exactly once.
 *
 * Why the exit code and not the program's stdout: ring-3 `capy_write(1, ...)`
 * lands on the debug-console (port 0xE9, see src/kernel/syscall.c::sys_write),
 * which the QEMU smokes capture but VMware does NOT. The VMware harness reads
 * COM1, so the authoritative marker must be emitted kernel-side. The exit
 * code is the program's signal to the kernel — the same mechanism the
 * thread-crash smoke uses for fault-killed exits.
 *
 * Mirrors thread_crash_smoke / scheduler_smoke: a pure latch (this header +
 * tls_handshake_smoke.c) the host tests exercise without freestanding wiring,
 * plus a separate I/O TU (tls_handshake_smoke_io.c) that emits the literal
 * COM1 marker. The live build feeds the latch from process_exit under
 * CAPYOS_TLS_HANDSHAKE_SMOKE.
 *
 * The marker string is observed verbatim by tools/scripts/smoke_x64_vmware.py
 * via `make smoke-x64-vmware-tls-handshake`. Do not rename it without updating
 * the smoke target and docs/operations/etapa-5-external-validation-playbook.md.
 */

#define TLS_HANDSHAKE_SMOKE_GATE_VERSION 1
#define TLS_HANDSHAKE_SMOKE_MARKER "[smoke] tls-handshake ready"
/* The tls_smoke program exits 0 only when the valid HTTPS GET succeeded AND
 * the invalid-cert GET failed closed. Any other outcome exits non-zero. */
#define TLS_HANDSHAKE_SMOKE_SUCCESS_EXIT_CODE 0

struct tls_handshake_smoke_state {
  /* Count of qualifying success exits (code == 0) observed since reset. */
  uint32_t successes_observed;
  /* Set to 1 once the marker has fired, so emission is exactly-once. */
  uint8_t emitted;
};

/* Reset a latch state to its initial zeroed form. Safe for repeated calls;
 * used by host tests and by the live build at boot. */
void tls_handshake_smoke_state_reset(struct tls_handshake_smoke_state *state);

/* Pure predicate shared by the latch and the host tests. Returns 1 when the
 * observed success count satisfies the gate (>= 1), 0 otherwise. */
int tls_handshake_smoke_gate_observed(uint32_t successes_observed);

/* Observe a process exit. Returns 1 on the single call that flips the gate
 * from "not ready" to "ready" (first code == 0 exit); returns 0 otherwise
 * (non-zero exit, gate already emitted, NULL state). Cheap enough to sit on
 * the process_exit path. */
int tls_handshake_smoke_observe_exit(struct tls_handshake_smoke_state *state,
                                     int32_t exit_code);

/* Process-wide single-emission latch (smoke-marker-pattern Invariant #5).
 * The live build drives this from process_exit (kernel/process.c); the reset
 * hook lets boot reset the global state. */
int tls_handshake_smoke_try_latch_exit_global(int32_t exit_code);
void tls_handshake_smoke_global_reset(void);

/* COM1 marker emission. Defined in tls_handshake_smoke_io.c so host tests
 * can override it; the live build forwards to com1_puts + klog. */
void tls_handshake_smoke_emit_marker(void);

#endif /* KERNEL_TLS_HANDSHAKE_SMOKE_H */
