#include "kernel/apps_roundtrip_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void apps_roundtrip_smoke_emit_marker(void) {
  com1_puts(APPS_ROUNDTRIP_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[apps] Etapa 6 Slice 6.6 apps-basic-roundtrip smoke marker emitted on COM1.");
}
