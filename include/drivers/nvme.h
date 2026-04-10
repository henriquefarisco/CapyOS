/* NVMe driver header for CAPYOS x86_64 kernel.
 * Supports NVMe 1.4 command set subset for disk I/O.
 */
#ifndef NVME_H
#define NVME_H

#include <stdint.h>

/* NVMe controller registers (offsets from BAR0) */
#define NVME_CAP 0x0000     /* Controller Capabilities (64-bit) */
#define NVME_VS 0x0008      /* Version */
#define NVME_INTMS 0x000C   /* Interrupt Mask Set */
#define NVME_INTMC 0x0010   /* Interrupt Mask Clear */
#define NVME_CC 0x0014      /* Controller Configuration */
#define NVME_CSTS 0x001C    /* Controller Status */
#define NVME_AQA 0x0024     /* Admin Queue Attributes */
#define NVME_ASQ 0x0028     /* Admin Submission Queue Base (64-bit) */
#define NVME_ACQ 0x0030     /* Admin Completion Queue Base (64-bit) */
#define NVME_SQ0TDBL 0x1000 /* Submission Queue 0 Tail Doorbell */

/* CC register bits */
#define NVME_CC_EN (1 << 0)       /* Enable */
#define NVME_CC_CSS_NVM (0 << 4)  /* NVM Command Set */
#define NVME_CC_MPS(n) ((n) << 7) /* Memory Page Size (2^(12+n)) */
#define NVME_CC_IOSQES (6 << 16)  /* I/O SQ Entry Size (64 bytes) */
#define NVME_CC_IOCQES (4 << 20)  /* I/O CQ Entry Size (16 bytes) */

/* CSTS register bits */
#define NVME_CSTS_RDY (1 << 0)   /* Ready */
#define NVME_CSTS_CFS (1 << 1)   /* Controller Fatal Status */
#define NVME_CSTS_SHST_MASK 0x0C /* Shutdown Status */

/* Admin command opcodes */
#define NVME_ADMIN_IDENTIFY 0x06
#define NVME_ADMIN_CREATE_IOSQ 0x01
#define NVME_ADMIN_CREATE_IOCQ 0x05
#define NVME_ADMIN_DELETE_IOSQ 0x00
#define NVME_ADMIN_DELETE_IOCQ 0x04

/* NVM command opcodes */
#define NVME_CMD_READ 0x02
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_FLUSH 0x00

/* Submission Queue Entry (64 bytes) */
struct nvme_sqe {
  uint8_t opcode;
  uint8_t flags;
  uint16_t cid;  /* Command ID */
  uint32_t nsid; /* Namespace ID */
  uint64_t rsvd1;
  uint64_t mptr; /* Metadata Pointer */
  uint64_t prp1; /* PRP Entry 1 */
  uint64_t prp2; /* PRP Entry 2 */
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} __attribute__((packed));

/* Completion Queue Entry (16 bytes) */
struct nvme_cqe {
  uint32_t result; /* Command specific result */
  uint32_t rsvd;
  uint16_t sq_head; /* SQ Head Pointer */
  uint16_t sq_id;   /* SQ Identifier */
  uint16_t cid;     /* Command Identifier */
  uint16_t status;  /* Status Field (P bit + Status) */
} __attribute__((packed));

/* Identify Controller data (partial) */
struct nvme_identify_ctrl {
  uint16_t vid;    /* PCI Vendor ID */
  uint16_t ssvid;  /* PCI Subsystem Vendor ID */
  char sn[20];     /* Serial Number */
  char mn[40];     /* Model Number */
  char fr[8];      /* Firmware Revision */
  uint8_t rab;     /* Recommended Arbitration Burst */
  uint8_t ieee[3]; /* IEEE OUI */
  uint8_t cmic;    /* Controller Multi-Path */
  uint8_t mdts;    /* Maximum Data Transfer Size */
  /* ... more fields, but we only need basic info ... */
  uint8_t pad[4096 - 78];
} __attribute__((packed));

/* NVMe device context */
struct nvme_device {
  volatile uint8_t *bar;    /* Memory-mapped BAR0 */
  uint32_t cap_lo;          /* CAP register low */
  uint32_t cap_hi;          /* CAP register high */
  uint32_t doorbell_stride; /* Doorbell stride (bytes) */

  /* Queues */
  struct nvme_sqe *admin_sq;
  struct nvme_cqe *admin_cq;
  uint16_t admin_sq_tail;
  uint16_t admin_cq_head;
  uint8_t admin_cq_phase;

  struct nvme_sqe *io_sq;
  struct nvme_cqe *io_cq;
  uint16_t io_sq_tail;
  uint16_t io_cq_head;
  uint8_t io_cq_phase;

  /* Namespace info */
  uint32_t nsid;
  uint64_t lba_count;
  uint32_t block_size;

  uint16_t next_cid; /* Next command ID */
};

/* Initialize NVMe subsystem. Returns 0 on success. */
int nvme_init(void);

/* Get NVMe device count */
int nvme_device_count(void);

/* Get block device for NVMe namespace */
struct block_device *nvme_get_block_device(int index);

/* Low-level NVMe operations (for advanced use) */
int nvme_read_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                     void *buffer);
int nvme_write_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                      const void *buffer);

#endif /* NVME_H */
