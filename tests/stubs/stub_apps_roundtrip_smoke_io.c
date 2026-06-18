#include "kernel/apps_roundtrip_smoke.h"

/* Host-test stub for the COM1 emitter: the latch logic is exercised by
 * tests/kernel/test_apps_roundtrip_smoke_gate.c without any serial I/O. */
void apps_roundtrip_smoke_emit_marker(void) { /* no-op for host tests */ }
