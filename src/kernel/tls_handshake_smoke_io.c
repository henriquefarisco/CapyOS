#include "kernel/tls_handshake_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void tls_handshake_smoke_emit_marker(void) {
  com1_puts(TLS_HANDSHAKE_SMOKE_MARKER "\n");
  klog(KLOG_INFO,
       "[net] Etapa 5 Slice 5.6 tls-handshake smoke marker emitted on COM1.");
}
