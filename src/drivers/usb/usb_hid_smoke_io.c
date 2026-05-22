/* Etapa 3 — Slice 3D external validation gate.
 * Kernel-only I/O side of the USB HID keyboard smoke marker.
 *
 * This translation unit is compiled into the x86_64 kernel only. Host
 * tests link the no-op stub at `tests/stubs/stub_usb_hid_smoke_io.c`
 * instead, so the gate logic in `usb_hid_smoke.c` remains pure and
 * host-testable.
 *
 * Two outputs are produced on transition:
 *   1. The canonical marker on COM1, watched by smoke_x64_vmware.py.
 *   2. A klog INFO entry so the trace is recoverable even when the
 *      serial capture is unavailable (forensic redundancy mirrors the
 *      DHCP marker pattern in network_bootstrap_config.c). */
#include "drivers/usb/usb_hid_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void usb_hid_keyboard_smoke_emit_marker(void) {
  com1_puts(USB_HID_KEYBOARD_SMOKE_MARKER "\n");
  klog(KLOG_INFO, "[usb-hid] Slice 3D smoke marker emitted on COM1.");
}
