#ifndef DRIVERS_STORAGE_AHCI_DISPATCH_H
#define DRIVERS_STORAGE_AHCI_DISPATCH_H

#include <stdint.h>

/*
 * Slice 3F (initial extraction) — pure dispatch-loop logic for
 * `ahci_exec_classified` in `src/drivers/storage/ahci.c`.
 *
 * The live AHCI command path spins on the controller's CI / IS /
 * TFD MMIO registers until exactly one of three outcomes can be
 * observed: the slot's CI bit is cleared (command completed), the
 * IS.TFES bit or TFD.ERR bit is set (command aborted), or neither
 * (command still in flight). The classification is a small pure
 * function over the four observed values; extracting it makes the
 * decision testable in host runners without MMIO, and prepares the
 * driver for the multi-slot dispatch landing in subsequent Slice
 * 3F fatias (where the inflight set is a bitmask rather than a
 * single slot).
 *
 * This header exposes:
 *
 *   - `enum ahci_dispatch_observation` — the closed three-state
 *     enumeration the dispatch loop acts on (INFLIGHT, COMPLETED,
 *     ABORTED).
 *   - `ahci_dispatch_classify_tick(...)` — pure mapping from
 *     (ci, is, tfd, slot_bit) to one observation. Matches the
 *     existing precedence in `ahci_exec_classified`: a cleared CI
 *     bit reports COMPLETED even if IS.TFES is also set (the
 *     controller has already retired the slot), and only when CI
 *     is still set does IS.TFES / TFD.ERR fire as ABORTED. This
 *     preserves bit-for-bit behaviour of the live spin loop.
 *   - `ahci_dispatch_completed_slots(...)` — pure bitmask
 *     difference for the multi-slot case: returns which slots
 *     transitioned from inflight to completed between two
 *     consecutive CI samples. Used by the future fan-in path; the
 *     single-slot case reduces to `(prev & slot_bit) &&
 *     !(cur & slot_bit)`.
 *
 * The header is intentionally tiny and deps-free (just `<stdint.h>`)
 * so host tests can link `ahci_dispatch.c` alone without MMIO,
 * klog, or PCIe headers.
 */

/* Observation enum is closed; adding a new value would force the
 * smoke marker to bump and the dispatch loop to add a handler.
 * Values are stable for tests to reference numerically. */
enum ahci_dispatch_observation {
  AHCI_DISPATCH_INFLIGHT = 0,
  AHCI_DISPATCH_COMPLETED = 1,
  AHCI_DISPATCH_ABORTED = 2,
};

/*
 * Single-slot tick classifier matching the precedence in
 * `ahci_exec_classified`:
 *
 *   1. If `(ci & slot_bit) == 0` -> COMPLETED. The controller has
 *      already cleared the CI bit, which means the slot retired
 *      (regardless of whether TFES/TFD.ERR were also raised; the
 *      caller still uses the classifier to decide if the retirement
 *      was clean or carried an error class).
 *   2. Else if `(is & AHCI_PORT_IS_TFES) || (tfd & AHCI_PORT_TFD_ERR)`
 *      -> ABORTED. The command faulted mid-flight; the slot stays
 *      flagged in CI but the host must release it after classifying.
 *   3. Else -> INFLIGHT. Keep spinning.
 *
 * Pure: no MMIO, no allocations. Constants for IS.TFES (bit 30) and
 * TFD.ERR (bit 0) are inlined in the implementation so the header
 * itself stays free of hardware register definitions.
 */
enum ahci_dispatch_observation
ahci_dispatch_classify_tick(uint32_t ci, uint32_t is, uint32_t tfd,
                            uint32_t slot_bit);

/*
 * Multi-slot completion fan-in. Given two consecutive samples of
 * the CI register and the set of slots the host believes are
 * inflight, returns the bitmask of slots that transitioned from
 * inflight to completed. The result is always a subset of
 * `inflight_mask`; bits cleared in `inflight_mask` are ignored
 * even if they appear to have transitioned (the host owns the
 * authoritative inflight set; the controller may clear CI bits
 * the host never set during reset paths and those must not be
 * mistaken for completions).
 *
 * Pure: no allocations. Useful for the IRQ-driven dispatch
 * landing in future Slice 3F fatias.
 */
uint32_t ahci_dispatch_completed_slots(uint32_t prev_ci, uint32_t cur_ci,
                                       uint32_t inflight_mask);

/*
 * Population count for a slot bitmask. Returns the number of bits
 * set in `inflight_mask`, i.e. the count of slots the host
 * believes are currently inflight. Stateless complement to
 * `ahci_slot_inflight_count` in `ahci_slot_allocator.h` (which
 * counts from the allocator's internal struct); use this when the
 * inflight set is materialised as a raw bitmask in the IRQ path
 * before being reconciled with the allocator.
 *
 * Pure: deterministic and branchless on the common case (small
 * popcount loop, no platform builtins to keep the kernel TU
 * portable across toolchains).
 */
uint32_t ahci_dispatch_inflight_count(uint32_t inflight_mask);

/*
 * Concurrency admission gate. Returns 1 if a new command may be
 * dispatched RIGHT NOW given the current `inflight_mask` and a
 * concurrency limit `concurrent_limit`:
 *
 *   - `concurrent_limit == 0` means "no limit" — admission always
 *     returns 1 (the caller is the slot allocator and its bitmap
 *     is the real ceiling).
 *   - `concurrent_limit > 0` caps the inflight set; admission
 *     returns 1 iff `popcount(inflight_mask) < concurrent_limit`.
 *
 * The cap is useful for backpressure tuning (e.g. throttle a slow
 * controller to 4 in-flight commands even though the hardware
 * advertises 32 slots) and for host tests that need to exercise
 * the "all-busy" branch deterministically. Stateless; no
 * allocations.
 */
int ahci_dispatch_can_admit(uint32_t inflight_mask, uint32_t concurrent_limit);

/*
 * Returns the lowest set bit index of `mask` (0..31) or -1 if
 * `mask == 0`. Useful in the IRQ fan-in path when
 * `ahci_dispatch_completed_slots` returns a multi-bit mask and the
 * host needs to dispatch the per-slot completion callbacks in a
 * deterministic order. The lowest-bit rule matches AHCI 1.3.1
 * §5.3.5 which recommends draining completions starting from slot
 * 0 when no ordering is implied by the issuer.
 *
 * Pure: branchless loop, no platform builtins.
 */
int ahci_dispatch_first_slot(uint32_t mask);

#endif /* DRIVERS_STORAGE_AHCI_DISPATCH_H */
