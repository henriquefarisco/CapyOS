#include "kernel/capymultifetch_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void capymultifetch_smoke_emit_marker(void) {
  com1_puts(CAPYMULTIFETCH_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[apps] Etapa 7 Slice 7.5 browser-multifetch smoke marker emitted on COM1.");
}
