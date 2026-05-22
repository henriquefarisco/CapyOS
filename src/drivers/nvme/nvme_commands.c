/* NVMe command builders — pure SQE encoders.
 *
 * Etapa 3 — Slice 3E.1: extracted from `src/drivers/nvme/nvme.c` to
 * allow host testing without driving real MMIO. The runtime driver
 * calls these builders and then submits the queue entry via the
 * doorbell.
 *
 * No MMIO, kmalloc, klog or any host-only dependency in this TU.
 */

#include "drivers/nvme/nvme_commands.h"

#include <stddef.h>

static void nvme_zero_sqe(struct nvme_sqe *cmd) {
  uint8_t *p = (uint8_t *)cmd;
  uint32_t i;
  for (i = 0u; i < sizeof(*cmd); ++i) {
    p[i] = 0;
  }
}

int nvme_build_identify_ctrl_cmd(struct nvme_sqe *cmd, void *identify_buffer) {
  if (cmd == NULL || identify_buffer == NULL) {
    return -1;
  }
  nvme_zero_sqe(cmd);
  cmd->opcode = NVME_ADMIN_IDENTIFY;
  cmd->nsid = 0u;
  cmd->prp1 = (uint64_t)(uintptr_t)identify_buffer;
  cmd->cdw10 = NVME_IDENTIFY_CNS_CONTROLLER;
  return 0;
}

int nvme_build_identify_ns_cmd(struct nvme_sqe *cmd, void *identify_buffer,
                               uint32_t nsid) {
  if (cmd == NULL || identify_buffer == NULL || nsid == 0u) {
    return -1;
  }
  nvme_zero_sqe(cmd);
  cmd->opcode = NVME_ADMIN_IDENTIFY;
  cmd->nsid = nsid;
  cmd->prp1 = (uint64_t)(uintptr_t)identify_buffer;
  cmd->cdw10 = NVME_IDENTIFY_CNS_NAMESPACE;
  return 0;
}

int nvme_build_create_cq_cmd(struct nvme_sqe *cmd, void *cq_buffer,
                             uint16_t qid, uint16_t qsize) {
  if (cmd == NULL || cq_buffer == NULL || qid == 0u || qsize == 0u) {
    return -1;
  }
  nvme_zero_sqe(cmd);
  cmd->opcode = NVME_ADMIN_CREATE_IOCQ;
  cmd->prp1 = (uint64_t)(uintptr_t)cq_buffer;
  /* CDW10[31:16]: QSIZE-1 (zero-based). CDW10[15:0]: QID. */
  cmd->cdw10 = ((uint32_t)(qsize - 1u) << 16) | (uint32_t)qid;
  /* CDW11[0]: PC = 1 (physically contiguous). Interrupts disabled
   * for the polling path (IEN=0, IV=0). */
  cmd->cdw11 = NVME_CREATE_QUEUE_PC_BIT;
  return 0;
}

int nvme_build_create_sq_cmd(struct nvme_sqe *cmd, void *sq_buffer,
                             uint16_t qid, uint16_t qsize, uint16_t cqid) {
  if (cmd == NULL || sq_buffer == NULL || qid == 0u || qsize == 0u) {
    return -1;
  }
  nvme_zero_sqe(cmd);
  cmd->opcode = NVME_ADMIN_CREATE_IOSQ;
  cmd->prp1 = (uint64_t)(uintptr_t)sq_buffer;
  cmd->cdw10 = ((uint32_t)(qsize - 1u) << 16) | (uint32_t)qid;
  /* CDW11[31:16]: CQID. CDW11[0]: PC=1. */
  cmd->cdw11 = ((uint32_t)cqid << 16) | NVME_CREATE_QUEUE_PC_BIT;
  return 0;
}

int nvme_build_rw_cmd(struct nvme_sqe *cmd, uint8_t opcode, uint32_t nsid,
                      uint64_t lba, uint32_t block_count, void *data_buffer) {
  if (cmd == NULL || data_buffer == NULL || nsid == 0u) {
    return -1;
  }
  if (opcode != NVME_CMD_READ && opcode != NVME_CMD_WRITE) {
    return -1;
  }
  if (block_count == 0u || block_count > 0x10000u) {
    return -1;
  }
  nvme_zero_sqe(cmd);
  cmd->opcode = opcode;
  cmd->nsid = nsid;
  cmd->prp1 = (uint64_t)(uintptr_t)data_buffer;
  cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
  cmd->cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFu);
  /* CDW12[15:0]: NLB (zero-based). */
  cmd->cdw12 = (block_count - 1u) & 0xFFFFu;
  return 0;
}
