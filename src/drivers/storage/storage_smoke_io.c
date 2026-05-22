/* Etapa 3 — Slice 3E.4 external validation gate.
 * Kernel-only I/O side of the storage stack smoke marker.
 *
 * Compiled into the x86_64 kernel only. Host tests link the
 * stub at `tests/stubs/stub_storage_smoke_io.c` so the gate
 * logic in `storage_smoke.c` remains pure and host-testable.
 *
 * Two outputs are produced on transition:
 *   1. The canonical marker on COM1, watched by the planned
 *      `smoke_x64_vmware_storage_resilience.py` harness (Slice 3E.5).
 *   2. A klog INFO entry so the trace is recoverable even if the
 *      serial capture is unavailable — mirrors the DHCP and USB HID
 *      smoke marker patterns. */

#include "drivers/storage/storage_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void storage_smoke_emit_marker(void) {
    com1_puts(STORAGE_SMOKE_MARKER "\n");
    klog(KLOG_INFO, "[storage] Slice 3E.4 smoke marker emitted on COM1.");
}
