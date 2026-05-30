#include "drivers/storage/ahci.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/pcie.h"
#include "drivers/storage/ahci_commands.h"
#include "drivers/storage/ahci_dispatch.h"
#include "drivers/storage/ahci_slot_allocator.h"
#include "drivers/storage/block_error.h"
#include "drivers/storage/storage_smoke.h"
#include "kernel/log/klog.h"

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

#define AHCI_MAX_PORTS 32
#define AHCI_GHC_AE (1u << 31)
#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)
#define AHCI_PORT_TFD_ERR 0x01u
#define AHCI_PORT_TFD_DRQ 0x08u
#define AHCI_PORT_TFD_BSY 0x80u
#define AHCI_PORT_IS_TFES (1u << 30)
#define AHCI_PORT_SIG_ATA 0x00000101u
#define AHCI_PX_SSTS_DET_MASK 0x0Fu
#define AHCI_PX_SSTS_DET_PRESENT 0x03u
#define AHCI_PX_SSTS_IPM_MASK 0x0F00u
#define AHCI_PX_SSTS_IPM_ACTIVE 0x0100u

#define ATA_CMD_IDENTIFY_DEVICE 0xECu
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u

/* AHCI_FIS_TYPE_REG_H2D moved to include/drivers/storage/ahci_commands.h
 * (shared with host tests). */
#define AHCI_TIMEOUT_SPINS 2000000u

/* Etapa 3 — Slice 3E.3: the runtime still owns a single command
 * table per port, so the allocator is configured for one slot.
 * When Slice 3F (or a future async dispatch) provisions N command
 * tables, switch this to the controller-reported CAP.NCS value. */
#define AHCI_RUNTIME_SLOT_COUNT 1u

struct ahci_hba_port {
  uint32_t clb;
  uint32_t clbu;
  uint32_t fb;
  uint32_t fbu;
  uint32_t is;
  uint32_t ie;
  uint32_t cmd;
  uint32_t rsv0;
  uint32_t tfd;
  uint32_t sig;
  uint32_t ssts;
  uint32_t sctl;
  uint32_t serr;
  uint32_t sact;
  uint32_t ci;
  uint32_t sntf;
  uint32_t fbs;
  uint32_t devslp;
  uint32_t rsv1[10];
  uint32_t vendor[4];
} __attribute__((packed));

struct ahci_hba_mem {
  uint32_t cap;
  uint32_t ghc;
  uint32_t is;
  uint32_t pi;
  uint32_t vs;
  uint32_t ccc_ctl;
  uint32_t ccc_pts;
  uint32_t em_loc;
  uint32_t em_ctl;
  uint32_t cap2;
  uint32_t bohc;
  uint8_t rsv[0xA0 - 0x2C];
  uint8_t vendor[0x100 - 0xA0];
  struct ahci_hba_port ports[AHCI_MAX_PORTS];
} __attribute__((packed));

/* struct ahci_cmd_header, ahci_prdt_entry, ahci_cmd_table moved to
 * include/drivers/storage/ahci_commands.h so host tests can exercise
 * the builders without including the runtime driver. */

struct ahci_port_ctx {
  struct ahci_hba_port *port;
  uint32_t port_no;
  uint64_t lba_count;
  struct block_device dev;
  struct ahci_cmd_header *cmd_list;
  uint8_t *rfis;
  struct ahci_cmd_table *cmd_table;
  uint16_t *identify_data;
  uint8_t *io_bounce;
  int initialized;
  /* Etapa 3 — Slice 3E.3: per-port slot allocator. Initialised to
   * AHCI_RUNTIME_SLOT_COUNT until we provision N command tables. */
  struct ahci_slot_allocator slot_alloc;
};

static struct ahci_hba_mem *g_ahci_hba = NULL;
static struct ahci_port_ctx g_ahci_ports[AHCI_MAX_PORTS];
static int g_ahci_count = 0;
static int g_ahci_scanned = 0;

/* Slice 3E.4.B (alpha.253) — local `dbg_puts`/`dbg_hex*` helpers
 * were removed in favor of `klog(level, ...)` / `klog_hex(...)`.
 * Output now lives in the in-memory klog ring (recoverable via
 * `klog_dump` and persisted by the kernel logger service)
 * instead of the QEMU-only port 0xE9 debug console. */

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

static inline uint32_t mmio_read32(volatile void *addr) {
  return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t value) {
  *(volatile uint32_t *)addr = value;
}

static int ahci_port_wait_idle(struct ahci_hba_port *port) {
  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    uint32_t tfd = mmio_read32(&port->tfd);
    if ((tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) {
      return 0;
    }
    cpu_relax();
  }
  return -1;
}

static int ahci_port_stop(struct ahci_hba_port *port) {
  uint32_t cmd = mmio_read32(&port->cmd);
  cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
  mmio_write32(&port->cmd, cmd);

  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    uint32_t cur = mmio_read32(&port->cmd);
    if ((cur & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) == 0u) {
      return 0;
    }
    cpu_relax();
  }
  return -1;
}

static int ahci_port_start(struct ahci_hba_port *port) {
  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    if ((mmio_read32(&port->cmd) & AHCI_PORT_CMD_CR) == 0u) {
      uint32_t cmd = mmio_read32(&port->cmd);
      cmd |= AHCI_PORT_CMD_FRE;
      mmio_write32(&port->cmd, cmd);
      cmd |= AHCI_PORT_CMD_ST;
      mmio_write32(&port->cmd, cmd);
      return 0;
    }
    cpu_relax();
  }
  return -1;
}

static int ahci_port_present(struct ahci_hba_port *port) {
  uint32_t ssts = mmio_read32(&port->ssts);
  uint32_t det = ssts & AHCI_PX_SSTS_DET_MASK;
  uint32_t ipm = ssts & AHCI_PX_SSTS_IPM_MASK;
  return det == AHCI_PX_SSTS_DET_PRESENT && ipm == AHCI_PX_SSTS_IPM_ACTIVE;
}

/* ahci_build_h2d_fis moved to src/drivers/storage/ahci_commands.c
 * (pure builder, host-testable). */

static enum block_io_error_class ahci_exec_classified(
    struct ahci_port_ctx *ctx, uint8_t command, uint64_t lba,
    uint16_t sector_count, void *buffer, uint32_t byte_count, int write) {
  struct ahci_hba_port *port = NULL;
  struct ahci_cmd_header *header = NULL;
  struct ahci_cmd_table *table = NULL;

  if (!ctx || !ctx->initialized || !buffer || sector_count == 0 ||
      byte_count == 0) {
    return BLOCK_IO_ERR_PERMANENT;
  }

  port = ctx->port;
  if (!port) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  if (!ahci_port_present(port)) {
    return BLOCK_IO_ERR_DEVICE_GONE;
  }
  if (ahci_port_wait_idle(port) != 0) {
    klog(KLOG_WARN, "[ahci] port not idle");
    klog_hex(KLOG_WARN, "[ahci] port.tfd=", mmio_read32(&port->tfd));
    klog_hex(KLOG_WARN, "[ahci] port.cmd=", mmio_read32(&port->cmd));
    return BLOCK_IO_ERR_TIMEOUT;
  }

  mmio_write32(&port->is, 0xFFFFFFFFu);
  mmio_write32(&port->serr, 0xFFFFFFFFu);

  /* Etapa 3 — Slice 3E.3: pick a slot from the allocator. The
   * runtime currently provisions only one command table so the
   * allocator returns slot 0; the call exercises the lifecycle so
   * that the multi-table dispatch in a future slice is a drop-in
   * change. */
  int slot = ahci_slot_alloc(&ctx->slot_alloc);
  if (slot < 0) {
    return BLOCK_IO_ERR_TRANSIENT;
  }
  header = &ctx->cmd_list[slot];
  table = ctx->cmd_table;

  /* Etapa 3 — Slice 3E.1: command header, FIS and PRDT are built
   * via the host-testable helpers in ahci_commands.c. */
  for (uint32_t i = 0; i < sizeof(*table); ++i) {
    ((uint8_t *)table)[i] = 0;
  }

  (void)ahci_build_command_header(header, (uint64_t)(uintptr_t)table,
                                  AHCI_H2D_FIS_LEN_DW, 1u, write);
  (void)ahci_build_h2d_fis(table->cfis, command, lba, sector_count);
  (void)ahci_build_prdt_entry(&table->prdt[0], (uint64_t)(uintptr_t)buffer,
                              byte_count, /*interrupt_on_complete=*/1);

  mmio_write32(&port->ci, 1u << slot);

  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    uint32_t ci = mmio_read32(&port->ci);
    uint32_t is = mmio_read32(&port->is);
    uint32_t tfd = mmio_read32(&port->tfd);
    /* Slice 3F initial extraction: the three-way precedence
     * (COMPLETED beats ABORTED beats INFLIGHT) lives in the pure
     * `ahci_dispatch_classify_tick` helper so host tests can lock
     * the bit semantics without an MMIO emulator. The downstream
     * `block_io_classify_ahci` still reads the same IS/TFD bits
     * to assign an error class. */
    enum ahci_dispatch_observation obs =
        ahci_dispatch_classify_tick(ci, is, tfd, 1u << slot);
    if (obs == AHCI_DISPATCH_COMPLETED) {
      /* Etapa 3 — Slice 3E.2: classify the outcome so callers (and
       * smoke logs) see a stable taxonomy. The legacy 0/-1 return
       * contract is preserved; the classifier is currently used to
       * annotate the diagnostic log only. Recoverable retry +
       * COMRESET escalation is deferred to Slice 3E.2.B. */
      enum block_io_error_class cls = block_io_classify_ahci(
          is, tfd, /*timed_out=*/0, ahci_port_present(port));
      if (cls != BLOCK_IO_OK) {
        klog(KLOG_WARN, "[ahci] command failed");
        klog(KLOG_WARN, block_io_error_class_name(cls));
        klog_hex(KLOG_WARN, "[ahci] port.is=", is);
        klog_hex(KLOG_WARN, "[ahci] port.tfd=", tfd);
      }
      (void)ahci_slot_release(&ctx->slot_alloc, slot);
      return cls;
    }
    if (obs == AHCI_DISPATCH_ABORTED) {
      enum block_io_error_class cls = block_io_classify_ahci(
          is, tfd, /*timed_out=*/0, ahci_port_present(port));
      klog(KLOG_WARN, "[ahci] command aborted");
      klog(KLOG_WARN, block_io_error_class_name(cls));
      klog_hex(KLOG_WARN, "[ahci] port.is=", is);
      klog_hex(KLOG_WARN, "[ahci] port.tfd=", tfd);
      (void)ahci_slot_release(&ctx->slot_alloc, slot);
      return cls == BLOCK_IO_OK ? BLOCK_IO_ERR_TRANSIENT : cls;
    }
    cpu_relax();
  }

  {
    uint32_t is_final = mmio_read32(&port->is);
    uint32_t tfd_final = mmio_read32(&port->tfd);
    enum block_io_error_class cls = block_io_classify_ahci(
        is_final, tfd_final, /*timed_out=*/1, ahci_port_present(port));
    klog(KLOG_WARN, "[ahci] command timeout");
    klog(KLOG_WARN, block_io_error_class_name(cls));
    klog_hex(KLOG_WARN, "[ahci] port.ci=", mmio_read32(&port->ci));
    klog_hex(KLOG_WARN, "[ahci] port.is=", is_final);
    klog_hex(KLOG_WARN, "[ahci] port.tfd=", tfd_final);
    /* Slot stays inflight on the controller side until COMRESET
     * recovery; the upper retry loop will call ahci_reset which
     * resets the allocator. We do NOT release here. */
    return cls;
  }
}

/* Legacy 0/-1 wrapper retained for the internal callers (identify,
 * read_block, write_block) that have not yet been converted to the
 * extended ABI. */
static int ahci_exec(struct ahci_port_ctx *ctx, uint8_t command, uint64_t lba,
                     uint16_t sector_count, void *buffer, uint32_t byte_count,
                     int write) {
  return ahci_exec_classified(ctx, command, lba, sector_count, buffer,
                              byte_count, write) == BLOCK_IO_OK
             ? 0
             : -1;
}

static int ahci_identify(struct ahci_port_ctx *ctx) {
  uint64_t lba48 = 0;
  uint32_t lba28 = 0;

  if (!ctx || !ctx->identify_data) {
    return -1;
  }
  if (ahci_exec(ctx, ATA_CMD_IDENTIFY_DEVICE, 0, 1, ctx->identify_data, 512u,
                0) != 0) {
    return -1;
  }

  lba28 = ((uint32_t)ctx->identify_data[61] << 16) | ctx->identify_data[60];
  lba48 = ((uint64_t)ctx->identify_data[103] << 48) |
          ((uint64_t)ctx->identify_data[102] << 32) |
          ((uint64_t)ctx->identify_data[101] << 16) |
          (uint64_t)ctx->identify_data[100];
  klog_hex(KLOG_INFO, "[ahci] identify lba28=", lba28);
  klog_hex(KLOG_INFO, "[ahci] identify lba48=", lba48);
  ctx->lba_count = lba48 ? lba48 : (uint64_t)lba28;
  return ctx->lba_count != 0 ? 0 : -1;
}

/* Etapa 3 — Slice 3E.4 (alpha.250) + alpha.252 audit fix BUG #1.
 *
 * The first OK completion across the whole storage stack fires
 * `[smoke] storage-stack ready` on COM1 + klog INFO. The latch
 * lives in `src/drivers/storage/storage_smoke.c` (global) so the
 * marker is emitted exactly once per boot regardless of which
 * controller wins the race. */
static void ahci_smoke_signal_ok(void) {
  if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI)) {
    storage_smoke_emit_marker();
  }
}

static enum block_io_error_class ahci_read_block_ex(void *opaque,
                                                    uint32_t block_no,
                                                    void *buffer) {
  struct ahci_port_ctx *ctx = (struct ahci_port_ctx *)opaque;
  enum block_io_error_class cls;
  if (!ctx || !buffer || (uint64_t)block_no >= ctx->lba_count ||
      !ctx->io_bounce) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  cls = ahci_exec_classified(ctx, ATA_CMD_READ_DMA_EXT, (uint64_t)block_no, 1,
                             ctx->io_bounce, 512u, 0);
  if (cls != BLOCK_IO_OK) {
    return cls;
  }
  for (uint32_t i = 0; i < 512u; ++i) {
    ((uint8_t *)buffer)[i] = ctx->io_bounce[i];
  }
  ahci_smoke_signal_ok();
  return BLOCK_IO_OK;
}

static enum block_io_error_class ahci_write_block_ex(void *opaque,
                                                     uint32_t block_no,
                                                     const void *buffer) {
  struct ahci_port_ctx *ctx = (struct ahci_port_ctx *)opaque;
  enum block_io_error_class cls;
  if (!ctx || !buffer || (uint64_t)block_no >= ctx->lba_count ||
      !ctx->io_bounce) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  for (uint32_t i = 0; i < 512u; ++i) {
    ctx->io_bounce[i] = ((const uint8_t *)buffer)[i];
  }
  cls = ahci_exec_classified(ctx, ATA_CMD_WRITE_DMA_EXT, (uint64_t)block_no,
                             1, ctx->io_bounce, 512u, 1);
  if (cls == BLOCK_IO_OK) {
    ahci_smoke_signal_ok();
  }
  return cls;
}

/* Legacy 0/-1 ops, used by callers that still go through
 * `block_device_read`/`block_device_write` directly without
 * inspecting the classifier. */
static int ahci_read_block(void *opaque, uint32_t block_no, void *buffer) {
  return ahci_read_block_ex(opaque, block_no, buffer) == BLOCK_IO_OK ? 0 : -1;
}

static int ahci_write_block(void *opaque, uint32_t block_no,
                            const void *buffer) {
  return ahci_write_block_ex(opaque, block_no, buffer) == BLOCK_IO_OK ? 0 : -1;
}

/* Etapa 3 — Slice 3E.2.B: COMRESET-based port reset triggered by the
 * unified retry loop when it observes BLOCK_IO_ERR_TIMEOUT. The
 * sequence follows AHCI 1.3.1 §10.4.2:
 *  1. Stop the port (PxCMD.ST=0, PxCMD.FRE=0).
 *  2. PxSCTL.DET=1 (start COMRESET).
 *  3. Wait ≥ 1 ms (we use a coarse spin since we have no msleep
 *     primitive in this path).
 *  4. PxSCTL.DET=0 (release COMRESET).
 *  5. Wait for PxSSTS.DET=3 (device detected, PHY ready) before
 *     restarting the port.
 *  6. Clear PxSERR and restart.
 *
 * Returns 0 if the port is usable for one retry, -1 if the device
 * never came back. In the latter case the retry loop surfaces the
 * timeout as PERMANENT and the caller can mark the device removed. */
static int ahci_port_comreset(struct ahci_port_ctx *ctx) {
  struct ahci_hba_port *port;
  uint32_t sctl;
  uint32_t ssts;
  if (!ctx || !ctx->initialized || !ctx->port) {
    return -1;
  }
  port = ctx->port;
  klog(KLOG_INFO, "[ahci] COMRESET begin");
  /* COMRESET drops all controller-side slot state. Reset the
   * allocator to match the controller's view. */
  ahci_slot_allocator_reset(&ctx->slot_alloc);
  (void)ahci_port_stop(port);
  /* DET field is bits [3:0] of PxSCTL. */
  sctl = mmio_read32(&port->sctl);
  mmio_write32(&port->sctl, (sctl & ~0x0Fu) | 0x01u);
  for (uint32_t i = 0; i < 200000u; ++i) {
    cpu_relax();
  }
  mmio_write32(&port->sctl, sctl & ~0x0Fu);
  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    ssts = mmio_read32(&port->ssts);
    if ((ssts & AHCI_PX_SSTS_DET_MASK) == AHCI_PX_SSTS_DET_PRESENT) {
      break;
    }
    cpu_relax();
  }
  ssts = mmio_read32(&port->ssts);
  if ((ssts & AHCI_PX_SSTS_DET_MASK) != AHCI_PX_SSTS_DET_PRESENT) {
    klog_hex(KLOG_ERROR, "[ahci] COMRESET no device, ssts=", ssts);
    return -1;
  }
  mmio_write32(&port->serr, 0xFFFFFFFFu);
  if (ahci_port_start(port) != 0) {
    klog(KLOG_ERROR, "[ahci] COMRESET restart failed");
    return -1;
  }
  klog(KLOG_INFO, "[ahci] COMRESET ok");
  return 0;
}

static int ahci_reset(void *opaque) {
  return ahci_port_comreset((struct ahci_port_ctx *)opaque);
}

static struct block_device_ops g_ahci_block_ops;
static int g_ahci_block_ops_initialized = 0;

static void ahci_init_block_ops(void) {
  if (g_ahci_block_ops_initialized) {
    return;
  }
  g_ahci_block_ops.read_block = ahci_read_block;
  g_ahci_block_ops.write_block = ahci_write_block;
  g_ahci_block_ops.read_block_ex = ahci_read_block_ex;
  g_ahci_block_ops.write_block_ex = ahci_write_block_ex;
  g_ahci_block_ops.reset = ahci_reset;
  g_ahci_block_ops_initialized = 1;
}

static int ahci_setup_port(struct ahci_port_ctx *ctx) {
  struct ahci_hba_port *port = NULL;

  if (!ctx || !ctx->port) {
    return -1;
  }

  port = ctx->port;
  klog_hex(KLOG_INFO, "[ahci] setup port=", ctx->port_no);
  klog_hex(KLOG_INFO, "[ahci] setup sig=", mmio_read32(&port->sig));
  klog_hex(KLOG_INFO, "[ahci] setup ssts=", mmio_read32(&port->ssts));
  klog_hex(KLOG_INFO, "[ahci] setup cmd=", mmio_read32(&port->cmd));
  if (ahci_port_stop(port) != 0) {
    klog(KLOG_ERROR, "[ahci] port stop timeout");
    klog_hex(KLOG_ERROR, "[ahci] port.cmd=", mmio_read32(&port->cmd));
    return -1;
  }

  /* Etapa 3 — Slice 3E.3: prime the per-port slot allocator. The
   * controller exposes CAP.NCS slots, but we only allocate one
   * command table for now, so we cap the allocator at 1. The
   * remaining slots become live when Slice 3F (or a future async
   * dispatch) provisions N command tables. */
  ahci_slot_allocator_init(&ctx->slot_alloc, AHCI_RUNTIME_SLOT_COUNT);

  ctx->cmd_list = (struct ahci_cmd_header *)kmalloc_aligned(1024u, 1024u);
  ctx->rfis = (uint8_t *)kmalloc_aligned(256u, 256u);
  ctx->cmd_table =
      (struct ahci_cmd_table *)kmalloc_aligned(sizeof(struct ahci_cmd_table), 128u);
  ctx->identify_data = (uint16_t *)kmalloc_aligned(512u, 512u);
  ctx->io_bounce = (uint8_t *)kmalloc_aligned(512u, 512u);
  if (!ctx->cmd_list || !ctx->rfis || !ctx->cmd_table || !ctx->identify_data ||
      !ctx->io_bounce) {
    if (ctx->io_bounce) {
      kfree_aligned(ctx->io_bounce);
      ctx->io_bounce = NULL;
    }
    if (ctx->identify_data) {
      kfree_aligned(ctx->identify_data);
      ctx->identify_data = NULL;
    }
    if (ctx->cmd_table) {
      kfree_aligned(ctx->cmd_table);
      ctx->cmd_table = NULL;
    }
    if (ctx->rfis) {
      kfree_aligned(ctx->rfis);
      ctx->rfis = NULL;
    }
    if (ctx->cmd_list) {
      kfree_aligned(ctx->cmd_list);
      ctx->cmd_list = NULL;
    }
    return -1;
  }

  for (uint32_t i = 0; i < 1024u; ++i) {
    ((uint8_t *)ctx->cmd_list)[i] = 0;
  }
  for (uint32_t i = 0; i < 256u; ++i) {
    ctx->rfis[i] = 0;
  }

  mmio_write32(&port->clb, (uint32_t)(uintptr_t)ctx->cmd_list);
  mmio_write32(&port->clbu, (uint32_t)((uint64_t)(uintptr_t)ctx->cmd_list >> 32));
  mmio_write32(&port->fb, (uint32_t)(uintptr_t)ctx->rfis);
  mmio_write32(&port->fbu, (uint32_t)((uint64_t)(uintptr_t)ctx->rfis >> 32));
  mmio_write32(&port->ie, 0u);
  mmio_write32(&port->is, 0xFFFFFFFFu);
  mmio_write32(&port->serr, 0xFFFFFFFFu);

  if (ahci_port_start(port) != 0) {
    klog(KLOG_ERROR, "[ahci] port start timeout");
    klog_hex(KLOG_ERROR, "[ahci] port.cmd=", mmio_read32(&port->cmd));
    if (ctx->io_bounce) {
      kfree_aligned(ctx->io_bounce);
      ctx->io_bounce = NULL;
    }
    if (ctx->identify_data) {
      kfree_aligned(ctx->identify_data);
      ctx->identify_data = NULL;
    }
    if (ctx->cmd_table) {
      kfree_aligned(ctx->cmd_table);
      ctx->cmd_table = NULL;
    }
    if (ctx->rfis) {
      kfree_aligned(ctx->rfis);
      ctx->rfis = NULL;
    }
    if (ctx->cmd_list) {
      kfree_aligned(ctx->cmd_list);
      ctx->cmd_list = NULL;
    }
    return -1;
  }

  ctx->initialized = 1;
  if (ahci_identify(ctx) != 0) {
    klog(KLOG_ERROR, "[ahci] identify failed");
    ctx->initialized = 0;
    if (ctx->io_bounce) {
      kfree_aligned(ctx->io_bounce);
      ctx->io_bounce = NULL;
    }
    if (ctx->identify_data) {
      kfree_aligned(ctx->identify_data);
      ctx->identify_data = NULL;
    }
    if (ctx->cmd_table) {
      kfree_aligned(ctx->cmd_table);
      ctx->cmd_table = NULL;
    }
    if (ctx->rfis) {
      kfree_aligned(ctx->rfis);
      ctx->rfis = NULL;
    }
    if (ctx->cmd_list) {
      kfree_aligned(ctx->cmd_list);
      ctx->cmd_list = NULL;
    }
    return -1;
  }

  ctx->dev.name = "ahci0";
  ctx->dev.block_size = 512u;
  ctx->dev.block_count =
      ctx->lba_count > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)ctx->lba_count;
  ctx->dev.ctx = ctx;
  ctx->dev.ops = &g_ahci_block_ops;
  return 0;
}

int ahci_init(void) {
  struct pci_device pci_dev;
  uint16_t cmd = 0;
  uint64_t abar = 0;
  uint32_t implemented = 0;

  if (g_ahci_scanned && g_ahci_count > 0) {
    return 0;
  }
  g_ahci_scanned = 1;
  g_ahci_count = 0;
  g_ahci_hba = NULL;
  ahci_init_block_ops();

  pci_init();
  if (pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, &pci_dev) != 0) {
    klog(KLOG_INFO, "[ahci] no SATA controller found");
    g_ahci_scanned = 0;
    return -1;
  }
  if (pci_dev.prog_if != 0x01u) {
    klog(KLOG_WARN, "[ahci] SATA controller is not AHCI");
    g_ahci_scanned = 0;
    return -1;
  }

  cmd = pci_config_read16(pci_dev.bus, pci_dev.device, pci_dev.function,
                          PCI_COMMAND);
  cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
  pci_config_write16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND,
                     cmd);

  abar = pci_read_bar64(pci_dev.bus, pci_dev.device, pci_dev.function, 5);
  if (abar == 0 || (abar & 0x1u) != 0u) {
    klog(KLOG_ERROR, "[ahci] invalid ABAR");
    g_ahci_scanned = 0;
    return -1;
  }

  g_ahci_hba = (struct ahci_hba_mem *)(uintptr_t)(abar & ~0xFULL);
  mmio_write32(&g_ahci_hba->ghc, mmio_read32(&g_ahci_hba->ghc) | AHCI_GHC_AE);

  implemented = mmio_read32(&g_ahci_hba->pi);
  klog_hex(KLOG_INFO, "[ahci] controller found, ABAR=", abar & ~0xFULL);
  klog_hex(KLOG_INFO, "[ahci] controller found, PI=", implemented);

  for (uint32_t port_no = 0; port_no < AHCI_MAX_PORTS; ++port_no) {
    struct ahci_port_ctx *ctx = NULL;
    if ((implemented & (1u << port_no)) == 0u) {
      continue;
    }
    ctx = &g_ahci_ports[g_ahci_count];
    ctx->port = &g_ahci_hba->ports[port_no];
    ctx->port_no = port_no;
    ctx->initialized = 0;
    if (!ahci_port_present(ctx->port) ||
        mmio_read32(&ctx->port->sig) != AHCI_PORT_SIG_ATA) {
      continue;
    }
    if (ahci_setup_port(ctx) != 0) {
      klog_hex(KLOG_ERROR, "[ahci] port setup failed, port=", port_no);
      continue;
    }
    g_ahci_count++;
    klog_hex(KLOG_INFO, "[ahci] SATA device ready, port=", port_no);
    klog_hex(KLOG_INFO, "[ahci] SATA device ready, sectors=", ctx->lba_count);
    if (g_ahci_count >= AHCI_MAX_PORTS) {
      break;
    }
  }

  if (g_ahci_count <= 0) {
    g_ahci_scanned = 0;
    klog(KLOG_WARN, "[ahci] no SATA device ready; retry allowed");
    return -1;
  }

  return 0;
}

int ahci_device_count(void) { return g_ahci_count; }

struct block_device *ahci_get_block_device(int index) {
  if (index < 0 || index >= g_ahci_count) {
    return NULL;
  }
  return &g_ahci_ports[index].dev;
}
