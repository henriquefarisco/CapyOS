#include "kernel/capygfx_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

/* Etapa 7 / Slice 7.2.2: COM1 marker emitter for the capygfx smoke. Kept in a
 * separate TU from the pure latch (capygfx_smoke.c) so host tests override it
 * with a no-op stub (tests/stubs/stub_capygfx_smoke_io.c). */
void capygfx_smoke_emit_marker(void) {
  com1_puts(CAPYGFX_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[gfx] Etapa 7 Slice 7.2.2 capygfx smoke marker emitted on COM1.");
}
