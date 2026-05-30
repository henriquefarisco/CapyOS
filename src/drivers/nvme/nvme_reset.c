#include "drivers/nvme/nvme_reset.h"

#include <stddef.h>

/* Sub-slice 3E.5.B (2026-05-25) — pure logic extracted from
 * nvme_controller_reset. See include/drivers/nvme/nvme_reset.h for
 * the contract and rationale. */

/* NVME_CSTS_RDY is bit 0 per NVMe 2.0 §3.1.6. CSTS.CFS is bit 1
 * (Controller Fatal Status). Both masks are duplicated here so the
 * pure TU does not depend on the public driver header (which
 * pulls in MMIO + PCIe). */
#define NVME_RESET_CSTS_RDY_MASK 0x1u
#define NVME_RESET_CSTS_CFS_MASK 0x2u

void nvme_reset_reprime_queue_state(struct nvme_reset_queue_state *qs) {
  if (!qs) return;
  qs->admin_sq_tail = 0u;
  qs->admin_cq_head = 0u;
  qs->admin_cq_phase = 1u;
  qs->io_sq_tail = 0u;
  qs->io_cq_head = 0u;
  qs->io_cq_phase = 1u;
}

int nvme_reset_csts_rdy_cleared(uint32_t csts) {
  return (csts & NVME_RESET_CSTS_RDY_MASK) == 0u ? 1 : 0;
}

int nvme_reset_csts_rdy_set(uint32_t csts) {
  return (csts & NVME_RESET_CSTS_RDY_MASK) != 0u ? 1 : 0;
}

int nvme_reset_csts_fatal(uint32_t csts) {
  return (csts & NVME_RESET_CSTS_CFS_MASK) != 0u ? 1 : 0;
}

enum nvme_reset_admin_action nvme_reset_next_admin_action(
    const struct nvme_reset_progress *p) {
  if (!p) {
    /* Defensive: callers that lose the progress struct should not
     * loop forever. The live driver always passes a real struct. */
    return NVME_RESET_ADMIN_DONE;
  }
  if (!p->io_cq_recreated) {
    /* BUG #2 fix: I/O CQ MUST be recreated first because the
     * controller validates the target CQ id during Create I/O SQ. */
    return NVME_RESET_ADMIN_CREATE_IO_CQ;
  }
  if (!p->io_sq_recreated) {
    return NVME_RESET_ADMIN_CREATE_IO_SQ;
  }
  return NVME_RESET_ADMIN_DONE;
}
