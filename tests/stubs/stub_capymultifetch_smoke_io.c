#include "kernel/capymultifetch_smoke.h"

/* Host-test stub for the COM1 emitter: the latch logic is exercised by
 * tests/kernel/test_capymultifetch_smoke_gate.c without any serial I/O. */
void capymultifetch_smoke_emit_marker(void) { /* no-op for host tests */ }
