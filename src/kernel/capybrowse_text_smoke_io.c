#include "kernel/capybrowse_text_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void capybrowse_text_smoke_emit_marker(void) {
  com1_puts(CAPYBROWSE_TEXT_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[apps] Etapa 6 Slice 6.4 capybrowse-text smoke marker emitted on COM1.");
}
