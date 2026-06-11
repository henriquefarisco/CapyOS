#include "kernel/tls_handshake_smoke.h"

/* Host-test stub for the COM1 emitter: the latch logic is exercised by
 * tests/kernel/test_tls_handshake_smoke_gate.c without any serial I/O. */
void tls_handshake_smoke_emit_marker(void) { /* no-op for host tests */ }
