#ifndef DRIVERS_STORAGE_STORAGE_SMOKE_H
#define DRIVERS_STORAGE_STORAGE_SMOKE_H
/* Etapa 3 — Slice 3E.4 external validation gate.
 *
 * Deterministic readiness gate for the planned
 * `smoke-x64-vmware-storage-resilience` external smoke target
 * (Slice 3E.5). The smoke harness watches the COM1 serial console
 * for the canonical marker; the kernel emits it once, only after
 * at least one storage controller (AHCI port or NVMe namespace)
 * has completed a real read OR write with BLOCK_IO_OK.
 *
 * Two halves must be observed:
 *   1. >=1 successful AHCI completion (PxCI cleared with no TFES /
 *      TFD.ERR; classifier returned BLOCK_IO_OK).
 *   2. OR >=1 successful NVMe I/O completion (CQE.SC=0, SCT=0).
 *
 * Either half satisfies the gate — a VM with only AHCI fires the
 * marker on the first AHCI OK; a VM with only NVMe fires on the
 * first NVMe OK; a dual-storage VM fires on whichever finishes
 * first. The state is latched so subsequent transitions never
 * re-emit.
 *
 * This module owns ONLY the gate logic. Emission to COM1 + klog
 * is wired in `src/drivers/storage/storage_smoke_io.c` (kernel
 * build) or stubbed in `tests/stubs/stub_storage_smoke_io.c`
 * (host build).
 */
#include <stdint.h>

#define STORAGE_SMOKE_GATE_VERSION 1
#define STORAGE_SMOKE_MARKER "[smoke] storage-stack ready"

enum storage_smoke_source {
    STORAGE_SMOKE_SRC_AHCI = 0,
    STORAGE_SMOKE_SRC_NVME = 1,
};

struct storage_smoke_state {
    uint32_t ahci_ok_count;
    uint32_t nvme_ok_count;
    uint8_t emitted;
};

void storage_smoke_state_reset(struct storage_smoke_state *state);

/* Pure gate predicate. Returns 1 iff at least one of the two
 * counters is non-zero. */
int storage_smoke_gate_observed(uint32_t ahci_ok_count,
                                uint32_t nvme_ok_count);

/* Composed transition checker. Increments the per-source counter
 * (AHCI or NVMe) and returns 1 exactly once: the first call in
 * which the gate transitions from blocked to observed AND the
 * latch is still cleared. Latches `emitted = 1` after returning
 * 1, so subsequent calls return 0 regardless of further updates.
 * Returns 0 on NULL state or invalid source. */
int storage_smoke_observe(struct storage_smoke_state *state,
                          enum storage_smoke_source source);

/* Emits STORAGE_SMOKE_MARKER followed by '\n' on COM1 and a
 * klog INFO entry. Kernel build implements this in
 * `src/drivers/storage/storage_smoke_io.c`; host test builds link
 * the stub at `tests/stubs/stub_storage_smoke_io.c`.
 *
 * Callers MUST guard each invocation with
 * `storage_smoke_try_latch_global` so that the marker is emitted
 * exactly once per boot, even across driver TUs. The function
 * performs no internal latching. */
void storage_smoke_emit_marker(void);

/* Process-wide single-emission latch. Returns 1 exactly once
 * across ALL callers in a boot (AHCI + NVMe combined); subsequent
 * calls return 0 regardless of source. Drivers call this on their
 * first BLOCK_IO_OK and emit the marker only if it returns 1.
 *
 * This closes the cross-driver double-emission gap that existed
 * when each driver carried its own per-TU latch (alpha.251 audit
 * BUG #1 — fixed in alpha.252). */
int storage_smoke_try_latch_global(enum storage_smoke_source source);

/* Host-test helper. Clears the global latch so the test order is
 * irrelevant. Production code must NOT call this. */
void storage_smoke_global_reset(void);

#endif /* DRIVERS_STORAGE_STORAGE_SMOKE_H */
