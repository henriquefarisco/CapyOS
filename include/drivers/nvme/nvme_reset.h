#ifndef DRIVERS_NVME_NVME_RESET_H
#define DRIVERS_NVME_NVME_RESET_H

#include <stdint.h>

/*
 * Sub-slice 3E.5.B — pure logic extracted from nvme_controller_reset
 * so the BUG #2 fix (recreate I/O CQ/SQ after CC.EN toggle) can be
 * locked by host tests. See `src/drivers/nvme/nvme.c::nvme_controller_reset`
 * for the live caller.
 *
 * The controller reset path on NVMe goes through five logical stages:
 *
 *   1. CC.EN <- 0 (host writes CC register)
 *   2. spin until CSTS.RDY == 0
 *   3. CC.EN <- 1 (host writes CC register)
 *   4. spin until CSTS.RDY == 1
 *   5. RECREATE I/O QUEUES via admin commands -- this is BUG #2.
 *
 * Stages 1-4 are MMIO and depend on a live PCIe BAR; they cannot be
 * exercised in host tests without a controller emulator. Stage 5 is
 * pure protocol bookkeeping (queue head/tail/phase + ordered admin
 * commands) and IS testable on the host.
 *
 * This header exposes:
 *
 *   - `struct nvme_reset_queue_state` -- the subset of `struct
 *     nvme_device` that must be reprimed after the CC.EN toggle.
 *   - `nvme_reset_reprime_queue_state(...)` -- the deterministic
 *     reprime operation (heads=0, tails=0, phases=1) the spec
 *     mandates because the controller discards its private state
 *     during the toggle.
 *   - `nvme_reset_csts_rdy_cleared(...)` / `nvme_reset_csts_rdy_set(...)`
 *     -- predicates against the NVMe CSTS register so the host can
 *     observe whether the spin loop satisfied its exit condition.
 *   - `enum nvme_reset_admin_action` + `nvme_reset_next_admin_action(...)`
 *     -- the BUG #2 contract: AFTER stage 4 completes, the host
 *     MUST issue `Create I/O CQ` followed by `Create I/O SQ`. The
 *     pure planner returns the next admin action given the
 *     observed progress; calling it in a loop and acting on the
 *     return value reproduces the live driver's recovery path
 *     bit-for-bit.
 *
 * The header is intentionally tiny and deps-free so host tests can
 * link against `nvme_reset.c` alone (no MMIO, no klog, no PCIe).
 */

/* Queue state subset that mirrors the post-reset zeroing block in
 * `nvme_controller_reset`. Field widths match
 * `include/drivers/nvme.h::struct nvme_device`. */
struct nvme_reset_queue_state {
  uint16_t admin_sq_tail;
  uint16_t admin_cq_head;
  uint8_t admin_cq_phase;
  uint16_t io_sq_tail;
  uint16_t io_cq_head;
  uint8_t io_cq_phase;
};

/* Reset the queue tracking state to the post-CC.EN-toggle baseline:
 *
 *   - tails and heads -> 0
 *   - phases -> 1  (the first CQE the controller posts after enable
 *                   carries phase tag 1; the host flips the local
 *                   tracker each time it consumes a CQE)
 *
 * Pure: no MMIO, no allocations. NULL-safe (no-op on NULL). */
void nvme_reset_reprime_queue_state(struct nvme_reset_queue_state *qs);

/* Predicate: CSTS.RDY bit is cleared. Returns 1 when stage 2 of the
 * reset spin loop should exit. */
int nvme_reset_csts_rdy_cleared(uint32_t csts);

/* Predicate: CSTS.RDY bit is set. Returns 1 when stage 4 of the
 * reset spin loop should exit. */
int nvme_reset_csts_rdy_set(uint32_t csts);

/* Predicate: CSTS.CFS (Controller Fatal Status) bit is set. The
 * reset spin loops MUST poll this in addition to RDY, otherwise a
 * controller that wedged into CFS during the reset would keep the
 * host spinning until the full 1M-iteration budget expires. NVMe
 * 2.0 §3.1.6 marks CFS as "the controller has encountered a fatal
 * error and is no longer able to process commands" — the only
 * valid recovery is a full Controller Level Reset (which is what
 * this path is trying to do!) followed by reinitialisation. If
 * CFS is observed mid-reset the host must surface the failure to
 * the upper retry loop immediately rather than burning the spin
 * budget. */
int nvme_reset_csts_fatal(uint32_t csts);

/* Admin actions the host must issue AFTER stage 4 completes. The
 * enumeration is closed and ordered: a caller polling this planner
 * MUST observe IO_CQ before IO_SQ (BUG #2 fix; the controller
 * rejects Create I/O SQ if its target CQ does not exist yet), and
 * MUST stop on DONE.
 *
 * The values are stable: tests reference them by name and an extra
 * action would force a smoke marker bump. */
enum nvme_reset_admin_action {
  NVME_RESET_ADMIN_DONE = 0,
  NVME_RESET_ADMIN_CREATE_IO_CQ = 1,
  NVME_RESET_ADMIN_CREATE_IO_SQ = 2,
};

/* Progress flags fed back into the planner after each admin command
 * succeeds. `io_cq_recreated` flips to 1 once the Create I/O CQ
 * admin command returned success; `io_sq_recreated` likewise.
 * Failure of either admin command is observed by the caller and
 * short-circuits the reset path (the planner is never asked again). */
struct nvme_reset_progress {
  uint8_t io_cq_recreated;
  uint8_t io_sq_recreated;
};

/* Returns the next admin action the host must issue. Pure: no I/O,
 * no allocations. NULL `p` returns DONE so accidental misuse cannot
 * loop forever. The BUG #2 contract this locks:
 *
 *   - empty progress           -> CREATE_IO_CQ
 *   - cq created, sq pending   -> CREATE_IO_SQ
 *   - cq created, sq created   -> DONE
 *   - any state where sq is
 *     "created" before cq is   -> CREATE_IO_CQ (force re-creation;
 *                                  this shape is unreachable from
 *                                  the live driver, defensive only) */
enum nvme_reset_admin_action nvme_reset_next_admin_action(
    const struct nvme_reset_progress *p);

#endif /* DRIVERS_NVME_NVME_RESET_H */
