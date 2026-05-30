#include "kernel/thread_crash_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void thread_crash_smoke_emit_marker(void) {
  com1_puts(THREAD_CRASH_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[scheduler] Etapa 4 Fase E smoke marker emitted on COM1.");
}
