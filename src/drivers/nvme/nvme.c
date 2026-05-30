/* NVMe driver implementation for CAPYOS x86_64.
 * Minimal implementation supporting single namespace read/write.
 */
#include "drivers/nvme.h"
#include "drivers/nvme/nvme_commands.h"
#include "drivers/nvme/nvme_reset.h"
#include "drivers/pcie.h"
#include "drivers/storage/block_error.h"
#include "drivers/storage/storage_smoke.h"
#include "fs/block.h"
#include "kernel/log/klog.h"
#include <stddef.h>
#include <stdint.h>

/* Slice 3E.4.B (alpha.253) — local `dbg_puts`/`dbg_hex*` helpers
 * were removed in favor of `klog(level, ...)` / `klog_hex(...)`.
 * As a side-effect this also closes a latent undefined-reference
 * to `dbg_label_hex32` (alpha.248 introduced two call sites in
 * `nvme_controller_reset` that referenced ahci.c's local helper). */

static inline void cpu_relax(void) { __asm__ volatile("pause"); }

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
#define NVME_QUEUE_DEPTH 64
#define NVME_PAGE_SIZE 4096
#define NVME_READY_TIMEOUT_SPINS 5000000
#define NVME_ADMIN_TIMEOUT_SPINS 20000000
#define NVME_IO_TIMEOUT_SPINS 20000000

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
  for (int i = 0; i < NVME_READY_TIMEOUT_SPINS; i++) {
    uint32_t csts = mmio_read32(base + NVME_CSTS);
    if (((csts & NVME_CSTS_RDY) != 0) == expected) {
      return 0;
    }
    if (csts & NVME_CSTS_CFS) {
      klog(KLOG_ERROR, "[nvme] controller fatal status");
      return -1;
    }
    cpu_relax();
  }
  klog(KLOG_ERROR, "[nvme] timeout waiting for ready");
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
  for (int i = 0; i < NVME_ADMIN_TIMEOUT_SPINS; i++) {
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

      /* Etapa 3 — Slice 3E.2: classify the completion so logs
       * carry a stable taxonomy. Legacy 0/-1 return preserved. */
      enum block_io_error_class cls =
          block_io_classify_nvme(status, /*timed_out=*/0);
      if (cls != BLOCK_IO_OK) {
        uint16_t sc = (status >> 1) & 0x7FF;
        klog(KLOG_ERROR, "[nvme] admin cmd failed");
        klog(KLOG_ERROR, block_io_error_class_name(cls));
        klog_hex(KLOG_ERROR, "[nvme] admin cmd sc=", sc);
        return -1;
      }
      return 0;
    }
    cpu_relax();
  }

  {
    enum block_io_error_class cls =
        block_io_classify_nvme(0, /*timed_out=*/1);
    klog(KLOG_ERROR, "[nvme] admin cmd timeout");
    klog(KLOG_ERROR, block_io_error_class_name(cls));
  }
  return -1;
}

/* Initialize NVMe controller */
static int nvme_init_controller(struct nvme_device *dev, uint64_t bar_addr) {
  dev->bar = (volatile uint8_t *)(uintptr_t)bar_addr;

  klog_hex(KLOG_INFO, "[nvme] BAR0=", bar_addr);

  /* Read capabilities */
  dev->cap_lo = mmio_read32(dev->bar + NVME_CAP);
  dev->cap_hi = mmio_read32(dev->bar + NVME_CAP + 4);

  /* Doorbell stride = 2^(2+DSTRD), DSTRD is bits 35:32 of CAP (bits 3:0 of
   * cap_hi) */
  uint32_t dstrd = dev->cap_hi & 0xF;
  dev->doorbell_stride = 4 << dstrd;

  klog_hex(KLOG_INFO, "[nvme] CAP_hi=", dev->cap_hi);
  klog_hex(KLOG_INFO, "[nvme] CAP_lo=", dev->cap_lo);
  klog_hex(KLOG_INFO, "[nvme] DSTRD=", dstrd);

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

  klog(KLOG_INFO, "[nvme] controller enabled");
  dev->next_cid = 1;

  return 0;
}

/* Identify controller */
static int nvme_identify(struct nvme_device *dev) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  /* Etapa 3 — Slice 3E.1: command-word construction lives in
   * src/drivers/nvme/nvme_commands.c so host tests can exercise the
   * exact layout without driving doorbells. */
  if (nvme_build_identify_ctrl_cmd(&cmd, g_identify_buf) != 0) {
    return -1;
  }

  if (nvme_admin_cmd(dev, &cmd, &cqe) != 0) {
    return -1;
  }

  struct nvme_identify_ctrl *ctrl = (struct nvme_identify_ctrl *)g_identify_buf;
  klog_hex(KLOG_INFO, "[nvme] VID=", ctrl->vid);
  klog_hex(KLOG_INFO, "[nvme] MDTS=", ctrl->mdts);

  return 0;
}

/* Identify namespace and get geometry */
static int nvme_identify_ns(struct nvme_device *dev, uint32_t nsid) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  if (nvme_build_identify_ns_cmd(&cmd, g_identify_buf, nsid) != 0) {
    return -1;
  }

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

  klog_hex(KLOG_INFO, "[nvme] NS=", nsid);
  klog_hex(KLOG_INFO, "[nvme] NS LBA_COUNT=", dev->lba_count);
  klog_hex(KLOG_INFO, "[nvme] NS BLOCK_SIZE=", dev->block_size);

  return 0;
}

/* Create I/O completion queue */
static int nvme_create_io_cq(struct nvme_device *dev, uint16_t qid) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  dev->io_cq = g_io_cq;
  dev->io_cq_head = 0;
  dev->io_cq_phase = 1;

  for (int i = 0; i < NVME_QUEUE_DEPTH; i++) {
    __builtin_memset(&g_io_cq[i], 0, sizeof(struct nvme_cqe));
  }

  if (nvme_build_create_cq_cmd(&cmd, dev->io_cq, qid,
                               (uint16_t)NVME_QUEUE_DEPTH) != 0) {
    return -1;
  }

  return nvme_admin_cmd(dev, &cmd, &cqe);
}

/* Create I/O submission queue */
static int nvme_create_io_sq(struct nvme_device *dev, uint16_t qid,
                             uint16_t cqid) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  dev->io_sq = g_io_sq;
  dev->io_sq_tail = 0;

  for (int i = 0; i < NVME_QUEUE_DEPTH; i++) {
    __builtin_memset(&g_io_sq[i], 0, sizeof(struct nvme_sqe));
  }

  if (nvme_build_create_sq_cmd(&cmd, dev->io_sq, qid,
                               (uint16_t)NVME_QUEUE_DEPTH, cqid) != 0) {
    return -1;
  }

  return nvme_admin_cmd(dev, &cmd, &cqe);
}

/* Submit I/O command and wait. Returns the classifier; the legacy
 * `nvme_io_cmd` wrapper collapses to 0/-1 for callers that have
 * not been ported to the extended ABI yet. */
static enum block_io_error_class nvme_io_cmd_classified(
    struct nvme_device *dev, struct nvme_sqe *cmd, struct nvme_cqe *cqe) {
  cmd->cid = dev->next_cid++;

  dev->io_sq[dev->io_sq_tail] = *cmd;
  dev->io_sq_tail = (dev->io_sq_tail + 1) % NVME_QUEUE_DEPTH;
  mb();

  nvme_ring_sq_doorbell(dev, 1, dev->io_sq_tail);

  for (int i = 0; i < NVME_IO_TIMEOUT_SPINS; i++) {
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

      enum block_io_error_class cls =
          block_io_classify_nvme(status, /*timed_out=*/0);
      if (cls != BLOCK_IO_OK) {
        uint16_t sc = (status >> 1) & 0x7FF;
        klog(KLOG_WARN, "[nvme] I/O cmd failed");
        klog(KLOG_WARN, block_io_error_class_name(cls));
        klog_hex(KLOG_WARN, "[nvme] I/O cmd sc=", sc);
      }
      return cls;
    }
    cpu_relax();
  }

  {
    enum block_io_error_class cls =
        block_io_classify_nvme(0, /*timed_out=*/1);
    klog(KLOG_WARN, "[nvme] I/O cmd timeout");
    klog(KLOG_WARN, block_io_error_class_name(cls));
    return cls;
  }
}

/* Legacy 0/-1 wrapper retained for callers (read/write internals)
 * that still use the original ABI. */
static int nvme_io_cmd(struct nvme_device *dev, struct nvme_sqe *cmd,
                       struct nvme_cqe *cqe) {
  return nvme_io_cmd_classified(dev, cmd, cqe) == BLOCK_IO_OK ? 0 : -1;
}

/* Read blocks from NVMe */
int nvme_read_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                     void *buffer) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  /* For simplicity, read one block at a time using internal buffer */
  uint8_t *dst = (uint8_t *)buffer;
  for (uint32_t i = 0; i < count; i++) {
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, dev->nsid, lba + i, 1u,
                          g_io_buffer) != 0) {
      return -1;
    }

    if (nvme_io_cmd(dev, &cmd, &cqe) != 0) {
      klog_hex(KLOG_WARN, "[nvme] read failed lba=", lba + i);
      return -1;
    }

    __builtin_memcpy(dst + i * dev->block_size, g_io_buffer, dev->block_size);
  }
  return 0;
}

/* Write blocks to NVMe */
int nvme_write_blocks(struct nvme_device *dev, uint64_t lba, uint32_t count,
                      const void *buffer) {
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;

  const uint8_t *src = (const uint8_t *)buffer;
  for (uint32_t i = 0; i < count; i++) {
    __builtin_memcpy(g_io_buffer, src + i * dev->block_size, dev->block_size);

    if (nvme_build_rw_cmd(&cmd, NVME_CMD_WRITE, dev->nsid, lba + i, 1u,
                          g_io_buffer) != 0) {
      return -1;
    }

    if (nvme_io_cmd(dev, &cmd, &cqe) != 0) {
      klog_hex(KLOG_WARN, "[nvme] write failed lba=", lba + i);
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

/* Slice 3E.2.B — extended I/O returning the classifier. We issue a
 * single-block command, mirroring the loop that nvme_read_blocks /
 * nvme_write_blocks already perform internally; doing it here lets
 * us surface the exact class instead of collapsing to -1. */
/* Etapa 3 — Slice 3E.4 (alpha.250) + alpha.252 audit fix BUG #1.
 * The latch lives in `storage_smoke.c` (global, shared with AHCI)
 * so the marker is emitted exactly once per boot. */
static void nvme_smoke_signal_ok(void) {
  if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_NVME)) {
    storage_smoke_emit_marker();
  }
}

static enum block_io_error_class nvme_block_read_ex(void *opaque,
                                                    uint32_t block_no,
                                                    void *buffer) {
  struct nvme_device *dev = (struct nvme_device *)opaque;
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;
  enum block_io_error_class cls;
  if (!dev || !buffer) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, dev->nsid, (uint64_t)block_no,
                        1u, g_io_buffer) != 0) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  cls = nvme_io_cmd_classified(dev, &cmd, &cqe);
  if (cls != BLOCK_IO_OK) {
    return cls;
  }
  __builtin_memcpy(buffer, g_io_buffer, dev->block_size);
  nvme_smoke_signal_ok();
  return BLOCK_IO_OK;
}

static enum block_io_error_class nvme_block_write_ex(void *opaque,
                                                     uint32_t block_no,
                                                     const void *buffer) {
  struct nvme_device *dev = (struct nvme_device *)opaque;
  struct nvme_sqe cmd;
  struct nvme_cqe cqe;
  enum block_io_error_class cls;
  if (!dev || !buffer) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  __builtin_memcpy(g_io_buffer, buffer, dev->block_size);
  if (nvme_build_rw_cmd(&cmd, NVME_CMD_WRITE, dev->nsid, (uint64_t)block_no,
                        1u, g_io_buffer) != 0) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  cls = nvme_io_cmd_classified(dev, &cmd, &cqe);
  if (cls == BLOCK_IO_OK) {
    nvme_smoke_signal_ok();
  }
  return cls;
}

/* Slice 3E.2.B — Controller Level Reset triggered by the unified
 * retry loop when it observes BLOCK_IO_ERR_TIMEOUT.
 *
 * Strategy: toggle CC.EN=0, wait for CSTS.RDY=0, then CC.EN=1 and
 * wait for CSTS.RDY=1. This is the heaviest recovery NVMe offers
 * short of NSSR; it loses in-flight commands by design, which is
 * acceptable because we only reach it after a TIMEOUT (the
 * controller already wedged).
 *
 * Per NVMe 2.0 §3.5.4 / 1.4 §7.3.1: across CC.EN toggle the
 * controller discards all I/O queue mappings. The DRAM buffers
 * (`g_admin_sq`, `g_admin_cq`, `g_io_sq`, `g_io_cq`) survive but
 * the controller's view of them does NOT — the host MUST reissue
 * Create I/O CQ + Create I/O SQ before the next I/O command, or
 * the retry will pend forever. This was alpha.251 audit BUG #2,
 * fixed in alpha.252 by calling `nvme_create_io_cq` /
 * `nvme_create_io_sq` here as part of the reset path.
 *
 * Admin queues survive because their base addresses live in the
 * AQA register, which is preserved across CC.EN unless the host
 * explicitly clears it; we do not touch AQA so the admin path is
 * usable for the Create I/O CQ/SQ commands themselves.
 *
 * Returns 0 if the controller came back ready AND the I/O queues
 * were re-registered, -1 if any step failed. */
static int nvme_controller_reset(struct nvme_device *dev) {
  uint32_t cc;
  uint32_t csts;
  struct nvme_reset_queue_state qs;
  struct nvme_reset_progress progress = {0u, 0u};
  if (!dev || !dev->bar) {
    return -1;
  }
  klog(KLOG_INFO, "[nvme] controller reset begin");
  cc = mmio_read32(dev->bar + NVME_CC);
  mmio_write32(dev->bar + NVME_CC, cc & ~NVME_CC_EN);
  /* Stage 2 spin: wait for CSTS.RDY=0 after CC.EN=0. Bail early
   * on CSTS.CFS (Controller Fatal Status) so a wedged controller
   * does not burn the full 1M-iteration budget — the host can
   * surface the failure to the upper retry loop immediately. */
  for (uint32_t spin = 0; spin < 1000000u; ++spin) {
    csts = mmio_read32(dev->bar + NVME_CSTS);
    if (nvme_reset_csts_fatal(csts)) {
      klog_hex(KLOG_ERROR, "[nvme] reset CSTS.CFS during disable, csts=",
               csts);
      return -1;
    }
    if (nvme_reset_csts_rdy_cleared(csts)) {
      break;
    }
    cpu_relax();
  }
  csts = mmio_read32(dev->bar + NVME_CSTS);
  if (nvme_reset_csts_fatal(csts)) {
    klog_hex(KLOG_ERROR, "[nvme] reset CSTS.CFS after disable, csts=", csts);
    return -1;
  }
  if (!nvme_reset_csts_rdy_cleared(csts)) {
    klog_hex(KLOG_ERROR, "[nvme] reset CSTS still RDY=", csts);
    return -1;
  }
  mmio_write32(dev->bar + NVME_CC, cc | NVME_CC_EN);
  /* Stage 4 spin: wait for CSTS.RDY=1 after CC.EN=1. Same CFS
   * early-exit applies; a controller that comes back with CFS=1
   * means the reset itself failed and the host must escalate
   * (likely a hardware-level reset path that this driver does
   * not implement). */
  for (uint32_t spin = 0; spin < 1000000u; ++spin) {
    csts = mmio_read32(dev->bar + NVME_CSTS);
    if (nvme_reset_csts_fatal(csts)) {
      klog_hex(KLOG_ERROR, "[nvme] reset CSTS.CFS during enable, csts=",
               csts);
      return -1;
    }
    if (nvme_reset_csts_rdy_set(csts)) {
      break;
    }
    cpu_relax();
  }
  csts = mmio_read32(dev->bar + NVME_CSTS);
  if (nvme_reset_csts_fatal(csts)) {
    klog_hex(KLOG_ERROR, "[nvme] reset CSTS.CFS after enable, csts=", csts);
    return -1;
  }
  if (!nvme_reset_csts_rdy_set(csts)) {
    klog_hex(KLOG_ERROR, "[nvme] reset RDY never came back, csts=", csts);
    return -1;
  }
  /* Reprime queue tracking through the pure helper so the host
   * tests can observe the exact baseline the controller expects
   * after the CC.EN toggle (heads/tails=0, phases=1). */
  nvme_reset_reprime_queue_state(&qs);
  dev->admin_sq_tail = qs.admin_sq_tail;
  dev->admin_cq_head = qs.admin_cq_head;
  dev->admin_cq_phase = qs.admin_cq_phase;
  dev->io_sq_tail = qs.io_sq_tail;
  dev->io_cq_head = qs.io_cq_head;
  dev->io_cq_phase = qs.io_cq_phase;
  /* alpha.252 audit fix BUG #2: recreate I/O queues via the pure
   * planner so the order (CQ first, then SQ) is locked by host
   * tests in tests/drivers/test_nvme_controller_reset.c. The
   * controller validates the target CQ id during Create I/O SQ;
   * inverting the order would burn the retry budget on every
   * timeout-triggered reset. */
  for (;;) {
    enum nvme_reset_admin_action action =
        nvme_reset_next_admin_action(&progress);
    if (action == NVME_RESET_ADMIN_DONE) {
      break;
    }
    if (action == NVME_RESET_ADMIN_CREATE_IO_CQ) {
      if (nvme_create_io_cq(dev, 1) != 0) {
        klog(KLOG_ERROR,
             "[nvme] controller reset: failed to recreate I/O CQ");
        return -1;
      }
      progress.io_cq_recreated = 1u;
      continue;
    }
    if (action == NVME_RESET_ADMIN_CREATE_IO_SQ) {
      if (nvme_create_io_sq(dev, 1, 1) != 0) {
        klog(KLOG_ERROR,
             "[nvme] controller reset: failed to recreate I/O SQ");
        return -1;
      }
      progress.io_sq_recreated = 1u;
      continue;
    }
    /* Unreachable: the planner enum is closed. Treat as failure to
     * stop the loop should a new action be added without wiring. */
    klog(KLOG_ERROR,
         "[nvme] controller reset: unknown admin action from planner");
    return -1;
  }
  klog(KLOG_INFO, "[nvme] controller reset ok (I/O queues restored)");
  return 0;
}

static int nvme_reset_op(void *opaque) {
  return nvme_controller_reset((struct nvme_device *)opaque);
}

static struct block_device_ops nvme_block_ops;
static int nvme_block_ops_initialized = 0;

static void nvme_init_block_ops(void) {
  if (nvme_block_ops_initialized) {
    return;
  }
  nvme_block_ops.read_block = nvme_block_read;
  nvme_block_ops.write_block = nvme_block_write;
  nvme_block_ops.read_block_ex = nvme_block_read_ex;
  nvme_block_ops.write_block_ex = nvme_block_write_ex;
  nvme_block_ops.reset = nvme_reset_op;
  nvme_block_ops_initialized = 1;
}

static struct block_device g_nvme_block_dev;

int nvme_init(void) {
  nvme_init_block_ops();
  struct pci_device pci_dev;

  klog(KLOG_INFO, "[nvme] scanning for NVMe controller...");

  pci_init();

  if (pci_find_nvme(&pci_dev) != 0) {
    klog(KLOG_INFO, "[nvme] no NVMe controller found");
    return -1;
  }

  klog_hex(KLOG_INFO, "[nvme] found NVMe bus=", pci_dev.bus);
  klog_hex(KLOG_INFO, "[nvme] found NVMe device=", pci_dev.device);
  klog_hex(KLOG_INFO, "[nvme] found NVMe function=", pci_dev.function);

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
    klog(KLOG_ERROR, "[nvme] BAR0 is zero");
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
    klog(KLOG_ERROR, "[nvme] failed to create I/O CQ");
    return -1;
  }
  if (nvme_create_io_sq(&g_nvme_dev, 1, 1) != 0) {
    klog(KLOG_ERROR, "[nvme] failed to create I/O SQ");
    return -1;
  }

  klog(KLOG_INFO, "[nvme] I/O queues created");

  /* Setup block device */
  g_nvme_block_dev.name = "nvme0n1";
  g_nvme_block_dev.block_size = g_nvme_dev.block_size;
  g_nvme_block_dev.block_count =
      (uint32_t)g_nvme_dev.lba_count; /* Truncate for 32-bit interface */
  g_nvme_block_dev.ctx = &g_nvme_dev;
  g_nvme_block_dev.ops = &nvme_block_ops;

  g_nvme_initialized = 1;
  klog(KLOG_INFO, "[nvme] init complete");

  return 0;
}

int nvme_device_count(void) { return g_nvme_initialized ? 1 : 0; }

struct block_device *nvme_get_block_device(int index) {
  if (index != 0 || !g_nvme_initialized) {
    return NULL;
  }
  return &g_nvme_block_dev;
}
