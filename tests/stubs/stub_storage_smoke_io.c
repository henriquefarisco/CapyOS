/* Host-test stub for the storage stack smoke marker emitter.
 *
 * The kernel build links `src/drivers/storage/storage_smoke_io.c`,
 * which writes the marker to COM1 + klog. Host tests cannot do
 * hardware I/O, so we provide a no-op implementation here. The
 * gate logic is exercised by `tests/drivers/test_storage_smoke_gate.c`
 * directly against `src/drivers/storage/storage_smoke.c`. */

#include "drivers/storage/storage_smoke.h"

void storage_smoke_emit_marker(void) {
    /* intentionally empty in host tests */
}
