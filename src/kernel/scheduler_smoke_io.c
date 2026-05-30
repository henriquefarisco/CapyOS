#include "kernel/scheduler_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void scheduler_fairness_smoke_emit_marker(void) {
  com1_puts(SCHEDULER_FAIRNESS_SMOKE_MARKER "\n");
  klog(KLOG_INFO, "[scheduler] Etapa 4 Fase C smoke marker emitted on COM1.");
}
