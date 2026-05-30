#include "gui/compositor_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void compositor_damage_smoke_emit_marker(void) {
  com1_puts(COMPOSITOR_DAMAGE_SMOKE_MARKER "\n");
  klog(KLOG_INFO, "[compositor] Etapa 4 Fase D smoke marker emitted on COM1.");
}
