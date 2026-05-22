/* NVMe command builders — pure SQE encoders.
 *
 * Etapa 3 — Slice 3E.1: extract the command-word population logic
 * out of `src/drivers/nvme/nvme.c` so the protocol layout can be
 * host-tested without driving real MMIO doorbells.
 *
 * References:
 *  - NVM Express Base Specification 1.4c, §5 (admin commands) and
 *    §6 (NVM commands).
 *  - Each builder writes into a caller-provided Submission Queue
 *    Entry (`struct nvme_sqe`). The caller is responsible for
 *    allocating the SQE, setting the `cid` (the runtime driver pulls
 *    it from its rolling counter) and ringing the doorbell.
 */
#ifndef DRIVERS_NVME_NVME_COMMANDS_H
#define DRIVERS_NVME_NVME_COMMANDS_H

#include <stdint.h>

#include "drivers/nvme.h"

/* CNS values for Identify (NVMe 1.4 §5.15.1, Figure 244). */
#define NVME_IDENTIFY_CNS_NAMESPACE 0x00u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u

/* Create I/O Queue command attributes (NVMe 1.4 §5.4, §5.5). */
#define NVME_CREATE_QUEUE_PC_BIT (1u << 0) /* Physically Contiguous */

/* Build an Identify Controller (CNS=0x01) admin command.
 * Writes opcode, nsid=0, prp1=buffer and cdw10=CNS to `cmd`. Other
 * fields are zeroed. Returns 0 on success, -1 on NULL `cmd` or
 * NULL `identify_buffer`. */
int nvme_build_identify_ctrl_cmd(struct nvme_sqe *cmd, void *identify_buffer);

/* Build an Identify Namespace (CNS=0x00) admin command for the
 * requested NSID. Returns 0 on success or -1 on invalid input
 * (NULL cmd / buffer, nsid == 0 which is reserved). */
int nvme_build_identify_ns_cmd(struct nvme_sqe *cmd, void *identify_buffer,
                               uint32_t nsid);

/* Build a Create I/O Completion Queue admin command.
 * `cq_buffer` is the physical address of the CQ memory; `qid` is the
 * desired queue identifier; `qsize` is the number of entries.
 * PC=1 (physically contiguous) is set. Returns 0 on success, -1 on
 * invalid input (NULL cmd / buffer, qid == 0 which is reserved for
 * admin, qsize == 0). */
int nvme_build_create_cq_cmd(struct nvme_sqe *cmd, void *cq_buffer,
                             uint16_t qid, uint16_t qsize);

/* Build a Create I/O Submission Queue admin command.
 * `sq_buffer` is the physical address of the SQ memory; `qid` is the
 * desired queue identifier; `qsize` is the number of entries;
 * `cqid` is the CQ this SQ is associated with. PC=1 is set.
 * Returns 0 on success, -1 on invalid input. */
int nvme_build_create_sq_cmd(struct nvme_sqe *cmd, void *sq_buffer,
                             uint16_t qid, uint16_t qsize, uint16_t cqid);

/* Build an NVM Read (opcode 0x02) or Write (opcode 0x01) command.
 * `opcode` MUST be one of NVME_CMD_READ / NVME_CMD_WRITE.
 * `nsid` is the target namespace; MUST be nonzero.
 * `lba` is the starting LBA (64-bit).
 * `block_count` is the number of logical blocks; the on-the-wire
 * NLB field is encoded as `block_count - 1` per NVMe §6.7.1.4.
 * `data_buffer` is the physical address of the data PRP.
 *
 * Returns 0 on success or -1 on invalid input (NULL cmd / buffer,
 * unsupported opcode, nsid == 0, block_count == 0 or block_count
 * > 65536 which exceeds the 16-bit NLB encoding). */
int nvme_build_rw_cmd(struct nvme_sqe *cmd, uint8_t opcode, uint32_t nsid,
                      uint64_t lba, uint32_t block_count, void *data_buffer);

#endif /* DRIVERS_NVME_NVME_COMMANDS_H */
