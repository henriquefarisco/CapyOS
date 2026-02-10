/* NVMe driver implementation for NoirOS x86_64.
 * Minimal implementation supporting single namespace read/write.
 */
#include "drivers/nvme.h"
#include "drivers/pcie.h"
#include "fs/block.h"
#include <stddef.h>
#include <stdint.h>

/* Debug output via port 0xE9 (works in QEMU/Bochs/debug builds) */
static inline void dbg_putc(char c) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}

static void dbg_puts(const char *s) {
  while (*s)
    dbg_putc(*s++);
}

static void dbg_hex32(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  for (int i = 28; i >= 0; i -= 4) {
    dbg_putc(hex[(v >> i) & 0xF]);
  }
}

static void dbg_hex64(uint64_t v) {
  dbg_hex32((uint32_t)(v >> 32));
  dbg_hex32((uint32_t)v);
}

/* Memory barrier */
static inline void mb(void) { __asm__ volatile("mfence" ::: "memory"); }

/* MMIO read/write */
static inline uint32_t mmio_read32(volatile void *addr) {
  return *(volatile uint32_t *)addr;
}
static inline void mmio_write32(volatile void *addr, uint32_t val) {
  *(volatile uint32_t *)addr = val;
  mb();
}
static inline __attribute__((unused)) uint64_t
mmio_read64(volatile void *addr) {
  volatile uint32_t *p = (volatile uint32_t *)addr;
  uint64_t lo = p[0];
  uint64_t hi = p[1];
  return lo | (hi << 32);
}
static inline void mmio_write64(volatile void *addr, uint64_t val) {
  volatile uint32_t *p = (volatile uint32_t *)addr;
  p[0] = (uint32_t)val;
  mb();
  p[1] = (uint32_t)(val >> 32);
  mb();
}

/* Simple aligned memory allocation (we'll use a static buffer for now) */
#define NVME_QUEUE_DEPTH 16
#define NVME_PAGE_SIZE 4096

/* Static buffers for queues (page-aligned) */
static struct nvme_sqe g_admin_sq[NVME_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static struct nvme_cqe g_admin_cq[NVME_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static struct nvme_sqe g_io_sq[NVME_QUEUE_DEPTH] __attribute__((aligned(4096)));
static struct nvme_cqe g_io_cq[NVME_QUEUE_DEPTH] __attribute__((aligned(4096)));
static uint8_t g_identify_buf[4096] __attribute__((aligned(4096)));
static uint8_t g_io_buffer[4096] __attribute__((aligned(4096)));

static struct nvme_device g_nvme_dev;
static int g_nvme_initialized = 0;

/* Wait for controller ready */
static int nvme_wait_ready(struct nvme_device *dev, int expected) {
  volatile uint8_t *base = dev->bar;
  for (int i = 0; i < 1000000; i++) {
    uint32_t csts = mmio_read32(base + NVME_CSTS);
    if (((csts & NVME_CSTS_RDY) != 0) == expected) {
      return 0;
    }
    if (csts & NVME_CSTS_CFS) {
      dbg_puts("[nvme] controller fatal status\n");
      return -1;
    }
  }
  dbg_puts("[nvme] timeout waiting for ready\n");
  return -1;
}

/* Ring doorbell for submission queue */
static void nvme_ring_sq_doorbell(struct nvme_device *dev, int qid,
                                  uint16_t tail) {
  uint32_t offset = 0x1000 + (qid * 2) * dev->doorbell_stride;
  mmio_write32(dev->bar + offset, tail);
}

/* Ring doorbell for completion queue head */
static void nvme_ring_cq_doorbell(struct nvme_device *dev, int qid,
                                  uint16_t head) {
  uint32_t offset = 0x1000 + (qid * 2 + 1) * dev->doorbell_stride;
  mmio_write32(dev->bar + offset, head);
}

/* Submit admin command and wait for completion */
static int nvme_admin_cmd(struct nvme_device *dev, struct nvme_sqe *cmd,
                          struct nvme_cqe *cqe) {
  /* Set command ID */
  cmd->cid = dev->next_cid++;

  /* Copy command to submission queue */
  dev->admin_sq[dev->admin_sq_tail] = *cmd;
  dev->admin_sq_tail = (dev->admin_sq_tail + 1) % NVME_QUEUE_DEPTH;
  mb();

  /* Ring doorbell */
  nvme_ring_sq_doorbell(dev, 0, dev->admin_sq_tail);

  /* Poll for completion */
  for (int i = 0; i < 1000000; i++) {
    struct nvme_cqe *entry = &dev->admin_cq[dev->admin_cq_head];
    uint16_t status = entry->status;
    int phase = status & 1;

    if (phase == dev->admin_cq_phase) {
      /* Completion received */
      if (cqe)
        *cqe = *entry;

      dev->admin_cq_head = (dev->admin_cq_head + 1) % NVME_QUEUE_DEPTH;
      if (dev->admin_cq_head == 0) {
        dev->admin_cq_phase ^= 1;
      }
      nvme_ring_cq_doorbell(dev, 0, dev->admin_cq_head);

      /* Check status (bits 1-14 are status code) */
      uint16_t sc = (status >> 1) & 0x7FF;
      if (sc != 0) {
        dbg_puts("[nvme] admin cmd failed, sc=");
        dbg_hex32(sc);
        dbg_putc('\n');
        return -1;
      }
      return 0;
    }
  }

  dbg_puts("[nvme] admin cmd timeout\n");
  return -1;
}

/* Initialize NVMe controller */
static int nvme_init_controller(struct nvme_device *dev, uint64_t bar_addr) {
  dev->bar = (volatile uint8_t *)(uintptr_t)bar_addr;

  dbg_puts("[nvme] BAR0=");
  dbg_hex64(bar_addr);
  dbg_putc('\n');

  /* Read capabilities */
  dev->cap_lo = mmio_read32(dev->bar + NVME_CAP);
  dev->cap_hi = mmio_read32(dev->bar + NVME_CAP + 4);

  /* Doorbell stride = 2^(2+DSTRD), DSTRD is bits 35:32 of CAP (bits 3:0 of
   * cap_hi) */
  uint32_t dstrd = dev->cap_hi & 0xF;
  dev->doorbell_stride = 4 << dstrd;

  dbg_puts("[nvme] CAP=");
  dbg_hex32(dev->cap_hi);
  dbg_hex32(dev->cap_lo);
  dbg_puts(" DSTRD=");
  dbg_hex32(dstrd);
  dbg_putc('\n');

  /* Disable controller */
  mmio_write32(dev->bar + NVME_CC, 0);
  if (nvme_wait_ready(dev, 0) != 0) {
    return -1;
  }

  /* Initialize queues */
  dev->admin_sq = g_admin_sq;
  dev->admin_cq = g_admin_cq;
  dev->admin_sq_tail = 0;
  dev->admin_cq_head = 0;
  dev->admin_cq_phase = 1;

  /* Zero queues */
  for (int i = 0; i < NVME_QUEUE_DEPTH; i++) {
    __builtin_memset(&g_admin_sq[i], 0, sizeof(struct nvme_sqe));
    __builtin_memset(&g_admin_cq[i], 0, sizeof(struct nvme_cqe));
  }

  /* Set admin queue addresses */
  uint64_t asq_addr = (uint64_t)(uintptr_t)dev->admin_sq;
  uint64_t acq_addr = (uint64_t)(uintptr_t)dev->admin_cq;

  mmio_write64(dev->bar + NVME_ASQ, asq_addr);
  mmio_write64(dev->bar + NVME_ACQ, acq_addr);

  /* Set admin queue attributes: size = NVME_QUEUE_DEPTH (0-based) */
  uint32_t aqa = ((NVME_QUEUE_DEPTH - 1) << 16) | (NVME_QUEUE_DEPTH - 1);
  mmio_write32(dev->bar + NVME_AQA, aqa);

  /* Enable controller */
  uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS(0) | NVME_CC_IOSQES |
                NVME_CC_IOCQES;
  mmio_write32(dev->bar + NVME_CC, cc);

  if (nvme_wait_ready(dev, 1) != 0) {
    return -1;
  }

  dbg_puts("[nvme] controller enabled\n");
  dev->next_cid = 1;

  return 0;
}

/* Identify controller */
static int nvme_identify(struct nvme_device *dev) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = 0;
  cmd.prp1 = (uint64_t)(uintptr_t)g_identify_buf;
  cmd.cdw10 = 1; /* CNS = 01h (Identify Controller) */

  if (nvme_admin_cmd(dev, &cmd, &cqe) != 0) {
    return -1;
  }

  struct nvme_identify_ctrl *ctrl = (struct nvme_identify_ctrl *)g_identify_buf;
  dbg_puts("[nvme] VID=");
  dbg_hex32(ctrl->vid);
  dbg_puts(" MDTS=");
  dbg_hex32(ctrl->mdts);
  dbg_putc('\n');

  return 0;
}

/* Identify namespace and get geometry */
static int nvme_identify_ns(struct nvme_device *dev, uint32_t nsid) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = nsid;
  cmd.prp1 = (uint64_t)(uintptr_t)g_identify_buf;
  cmd.cdw10 = 0; /* CNS = 00h (Identify Namespace) */

  if (nvme_admin_cmd(dev, &cmd, &cqe) != 0) {
    return -1;
  }

  /* Parse namespace data */
  uint64_t *ns_data = (uint64_t *)g_identify_buf;
  dev->lba_count = ns_data[0]; /* NSZE (offset 0) */

  /* Get LBA format (offset 0x1A) */
  uint8_t flbas = g_identify_buf[26] & 0x0F;
  uint32_t *lbaf = (uint32_t *)(g_identify_buf + 128 + flbas * 4);
  uint8_t lba_ds = (*lbaf >> 16) & 0xFF; /* LBA Data Size as power of 2 */
  dev->block_size = 1u << lba_ds;

  dev->nsid = nsid;

  dbg_puts("[nvme] NS");
  dbg_hex32(nsid);
  dbg_puts(" LBA_COUNT=");
  dbg_hex64(dev->lba_count);
  dbg_puts(" BLOCK_SIZE=");
  dbg_hex32(dev->block_size);
  dbg_putc('\n');

  return 0;
}

/* Create I/O completion queue */
static int nvme_create_io_cq(struct nvme_device *dev, uint16_t qid) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  dev->io_cq = g_io_cq;
  dev->io_cq_head = 0;
  dev->io_cq_phase = 1;

  for (int i = 0; i < NVME_QUEUE_DEPTH; i++) {
    __builtin_memset(&g_io_cq[i], 0, sizeof(struct nvme_cqe));
  }

  cmd.opcode = NVME_ADMIN_CREATE_IOCQ;
  cmd.prp1 = (uint64_t)(uintptr_t)dev->io_cq;
  cmd.cdw10 = ((NVME_QUEUE_DEPTH - 1) << 16) | qid;
  cmd.cdw11 = 1; /* PC=1 (physically contiguous) */

  return nvme_admin_cmd(dev, &cmd, &cqe);
}

/* Create I/O submission queue */
static int nvme_create_io_sq(struct nvme_device *dev, uint16_t qid,
                             uint16_t cqid) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  dev->io_sq = g_io_sq;
  dev->io_sq_tail = 0;

  for (int i = 0; i < NVME_QUEUE_DEPTH; i++) {
    __builtin_memset(&g_io_sq[i], 0, sizeof(struct nvme_sqe));
  }

  cmd.opcode = NVME_ADMIN_CREATE_IOSQ;
  cmd.prp1 = (uint64_t)(uintptr_t)dev->io_sq;
  cmd.cdw10 = ((NVME_QUEUE_DEPTH - 1) << 16) | qid;
  cmd.cdw11 = (cqid << 16) | 1; /* CQID in bits 31:16, PC=1 */

  return nvme_admin_cmd(dev, &cmd, &cqe);
}

/* Submit I/O command and wait */
static int nvme_io_cmd(struct nvme_device *dev, struct nvme_sqe *cmd,
                       struct nvme_cqe *cqe) {
  cmd->cid = dev->next_cid++;

  dev->io_sq[dev->io_sq_tail] = *cmd;
  dev->io_sq_tail = (dev->io_sq_tail + 1) % NVME_QUEUE_DEPTH;
  mb();

  nvme_ring_sq_doorbell(dev, 1, dev->io_sq_tail);

  for (int i = 0; i < 1000000; i++) {
    struct nvme_cqe *entry = &dev->io_cq[dev->io_cq_head];
    uint16_t status = entry->status;
    int phase = status & 1;

    if (phase == dev->io_cq_phase) {
      if (cqe)
        *cqe = *entry;

      dev->io_cq_head = (dev->io_cq_head + 1) % NVME_QUEUE_DEPTH;
      if (dev->io_cq_head == 0) {
        dev->io_cq_phase ^= 1;
      }
      nvme_ring_cq_doorbell(dev, 1, dev->io_cq_head);

      uint16_t sc = (status >> 1) & 0x7FF;
      if (sc != 0) {
        dbg_puts("[nvme] I/O cmd failed, sc=");
        dbg_hex32(sc);
        dbg_putc('\n');
        return -1;
      }
      return 0;
    }
  }

  dbg_puts("[nvme] I/O cmd timeout\n");
  return -1;
}

/* Read blocks from NVMe */
int nvme_read_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                     void *buffer) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  /* For simplicity, read one block at a time using internal buffer */
  uint8_t *dst = (uint8_t *)buffer;
  for (uint32_t i = 0; i < count; i++) {
    cmd.opcode = NVME_CMD_READ;
    cmd.nsid = dev->nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)g_io_buffer;
    cmd.cdw10 = (uint32_t)(lba + i);
    cmd.cdw11 = (uint32_t)((lba + i) >> 32);
    cmd.cdw12 = 0; /* NLB = 0 means 1 block */

    if (nvme_io_cmd(dev, &cmd, &cqe) != 0) {
      return -1;
    }

    __builtin_memcpy(dst + i * dev->block_size, g_io_buffer, dev->block_size);
  }
  return 0;
}

/* Write blocks to NVMe */
int nvme_write_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                      const void *buffer) {
  struct nvme_sqe cmd = {0};
  struct nvme_cqe cqe;

  const uint8_t *src = (const uint8_t *)buffer;
  for (uint32_t i = 0; i < count; i++) {
    __builtin_memcpy(g_io_buffer, src + i * dev->block_size, dev->block_size);

    cmd.opcode = NVME_CMD_WRITE;
    cmd.nsid = dev->nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)g_io_buffer;
    cmd.cdw10 = (uint32_t)(lba + i);
    cmd.cdw11 = (uint32_t)((lba + i) >> 32);
    cmd.cdw12 = 0; /* NLB = 0 means 1 block */

    if (nvme_io_cmd(dev, &cmd, &cqe) != 0) {
      return -1;
    }
  }
  return 0;
}

/* Block device interface */
static int nvme_block_read(void *ctx, uint32_t block_no, void *buffer) {
  struct nvme_device *dev = (struct nvme_device *)ctx;
  return nvme_read_blocks(dev, block_no, 1, buffer);
}

static int nvme_block_write(void *ctx, uint32_t block_no, const void *buffer) {
  struct nvme_device *dev = (struct nvme_device *)ctx;
  return nvme_write_blocks(dev, block_no, 1, buffer);
}

static struct block_device_ops nvme_block_ops;
static int nvme_block_ops_initialized = 0;

static void nvme_init_block_ops(void) {
  if (nvme_block_ops_initialized) {
    return;
  }
  nvme_block_ops.read_block = nvme_block_read;
  nvme_block_ops.write_block = nvme_block_write;
  nvme_block_ops_initialized = 1;
}

static struct block_device g_nvme_block_dev;

int nvme_init(void) {
  nvme_init_block_ops();
  struct pci_device pci_dev;

  dbg_puts("[nvme] scanning for NVMe controller...\n");

  pci_init();

  if (pci_find_nvme(&pci_dev) != 0) {
    dbg_puts("[nvme] no NVMe controller found\n");
    return -1;
  }

  dbg_puts("[nvme] found NVMe at ");
  dbg_hex32(pci_dev.bus);
  dbg_putc(':');
  dbg_hex32(pci_dev.device);
  dbg_putc('.');
  dbg_hex32(pci_dev.function);
  dbg_putc('\n');

  /* Enable bus mastering and memory access */
  uint16_t cmd = pci_config_read16(pci_dev.bus, pci_dev.device,
                                   pci_dev.function, PCI_COMMAND);
  cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
  pci_config_write16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND,
                     cmd);

  /* Get BAR0 (NVMe memory-mapped registers) */
  uint64_t bar0 =
      pci_read_bar64(pci_dev.bus, pci_dev.device, pci_dev.function, 0);
  if (bar0 == 0) {
    dbg_puts("[nvme] BAR0 is zero\n");
    return -1;
  }

  /* Initialize controller */
  if (nvme_init_controller(&g_nvme_dev, bar0) != 0) {
    return -1;
  }

  /* Identify controller */
  if (nvme_identify(&g_nvme_dev) != 0) {
    return -1;
  }

  /* Identify namespace 1 */
  if (nvme_identify_ns(&g_nvme_dev, 1) != 0) {
    return -1;
  }

  /* Create I/O queues */
  if (nvme_create_io_cq(&g_nvme_dev, 1) != 0) {
    dbg_puts("[nvme] failed to create I/O CQ\n");
    return -1;
  }
  if (nvme_create_io_sq(&g_nvme_dev, 1, 1) != 0) {
    dbg_puts("[nvme] failed to create I/O SQ\n");
    return -1;
  }

  dbg_puts("[nvme] I/O queues created\n");

  /* Setup block device */
  g_nvme_block_dev.name = "nvme0n1";
  g_nvme_block_dev.block_size = g_nvme_dev.block_size;
  g_nvme_block_dev.block_count =
      (uint32_t)g_nvme_dev.lba_count; /* Truncate for 32-bit interface */
  g_nvme_block_dev.ctx = &g_nvme_dev;
  g_nvme_block_dev.ops = &nvme_block_ops;

  g_nvme_initialized = 1;
  dbg_puts("[nvme] init complete\n");

  return 0;
}

int nvme_device_count(void) { return g_nvme_initialized ? 1 : 0; }

struct block_device *nvme_get_block_device(int index) {
  if (index != 0 || !g_nvme_initialized) {
    return NULL;
  }
  return &g_nvme_block_dev;
}
