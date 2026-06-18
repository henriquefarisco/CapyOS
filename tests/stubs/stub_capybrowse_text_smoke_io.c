#include "kernel/capybrowse_text_smoke.h"

/* Host-test stub for the COM1 emitter: the latch logic is exercised by
 * tests/kernel/test_capybrowse_text_smoke_gate.c without any serial I/O. */
void capybrowse_text_smoke_emit_marker(void) { /* no-op for host tests */ }
