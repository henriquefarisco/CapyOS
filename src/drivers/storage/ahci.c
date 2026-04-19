#include "drivers/storage/ahci.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/pcie.h"

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

#define AHCI_FIS_TYPE_REG_H2D 0x27u
#define AHCI_CMD_SLOT 0u
#define AHCI_TIMEOUT_SPINS 2000000u

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

struct ahci_cmd_header {
  uint16_t flags;
  uint16_t prdtl;
  uint32_t prdbc;
  uint32_t ctba;
  uint32_t ctbau;
  uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
  uint32_t dba;
  uint32_t dbau;
  uint32_t reserved0;
  uint32_t dbc_i;
} __attribute__((packed));

struct ahci_cmd_table {
  uint8_t cfis[64];
  uint8_t acmd[16];
  uint8_t reserved[48];
  struct ahci_prdt_entry prdt[1];
} __attribute__((packed));

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
};

static struct ahci_hba_mem *g_ahci_hba = NULL;
static struct ahci_port_ctx g_ahci_ports[AHCI_MAX_PORTS];
static int g_ahci_count = 0;
static int g_ahci_scanned = 0;

static inline void dbg_putc(char c) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}

static void dbg_puts(const char *s) {
  while (s && *s) {
    dbg_putc(*s++);
  }
}

static void dbg_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    dbg_putc(hex[(value >> shift) & 0xFu]);
  }
}

static void dbg_hex64(uint64_t value) {
  dbg_hex32((uint32_t)(value >> 32));
  dbg_hex32((uint32_t)value);
}

static void dbg_label_hex32(const char *label, uint32_t value) {
  dbg_puts(label);
  dbg_hex32(value);
  dbg_putc('\n');
}

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

static void ahci_build_h2d_fis(uint8_t *cfis, uint8_t command, uint64_t lba,
                               uint16_t sector_count) {
  for (uint32_t i = 0; i < 64u; ++i) {
    cfis[i] = 0;
  }
  cfis[0] = AHCI_FIS_TYPE_REG_H2D;
  cfis[1] = 1u << 7;
  cfis[2] = command;
  cfis[4] = (uint8_t)(lba & 0xFFu);
  cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
  cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
  cfis[7] = 1u << 6; /* LBA mode */
  cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
  cfis[9] = (uint8_t)((lba >> 32) & 0xFFu);
  cfis[10] = (uint8_t)((lba >> 40) & 0xFFu);
  cfis[12] = (uint8_t)(sector_count & 0xFFu);
  cfis[13] = (uint8_t)((sector_count >> 8) & 0xFFu);
}

static int ahci_exec(struct ahci_port_ctx *ctx, uint8_t command, uint64_t lba,
                     uint16_t sector_count, void *buffer, uint32_t byte_count,
                     int write) {
  struct ahci_hba_port *port = NULL;
  struct ahci_cmd_header *header = NULL;
  struct ahci_cmd_table *table = NULL;

  if (!ctx || !ctx->initialized || !buffer || sector_count == 0 ||
      byte_count == 0) {
    return -1;
  }

  port = ctx->port;
  if (!port) {
    return -1;
  }
  if (ahci_port_wait_idle(port) != 0) {
    dbg_puts("[ahci] port not idle\n");
    dbg_label_hex32("[ahci] port.tfd=", mmio_read32(&port->tfd));
    dbg_label_hex32("[ahci] port.cmd=", mmio_read32(&port->cmd));
    return -1;
  }

  mmio_write32(&port->is, 0xFFFFFFFFu);
  mmio_write32(&port->serr, 0xFFFFFFFFu);

  header = &ctx->cmd_list[AHCI_CMD_SLOT];
  table = ctx->cmd_table;

  header->flags = 5u | (write ? (1u << 6) : 0u);
  header->prdtl = 1u;
  header->prdbc = 0u;
  header->ctba = (uint32_t)(uintptr_t)table;
  header->ctbau = (uint32_t)((uint64_t)(uintptr_t)table >> 32);
  for (uint32_t i = 0; i < 4u; ++i) {
    header->reserved[i] = 0u;
  }

  for (uint32_t i = 0; i < sizeof(*table); ++i) {
    ((uint8_t *)table)[i] = 0;
  }

  ahci_build_h2d_fis(table->cfis, command, lba, sector_count);
  table->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
  table->prdt[0].dbau = (uint32_t)((uint64_t)(uintptr_t)buffer >> 32);
  table->prdt[0].reserved0 = 0u;
  table->prdt[0].dbc_i = ((byte_count - 1u) & 0x003FFFFFu) | (1u << 31);

  mmio_write32(&port->ci, 1u << AHCI_CMD_SLOT);

  for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
    uint32_t ci = mmio_read32(&port->ci);
    uint32_t is = mmio_read32(&port->is);
    uint32_t tfd = mmio_read32(&port->tfd);
    if ((ci & (1u << AHCI_CMD_SLOT)) == 0u) {
      if ((is & AHCI_PORT_IS_TFES) != 0u ||
          (tfd & AHCI_PORT_TFD_ERR) != 0u) {
        dbg_puts("[ahci] command completed with error\n");
        dbg_label_hex32("[ahci] port.is=", is);
        dbg_label_hex32("[ahci] port.tfd=", tfd);
        return -1;
      }
      return 0;
    }
    if ((is & AHCI_PORT_IS_TFES) != 0u || (tfd & AHCI_PORT_TFD_ERR) != 0u) {
      dbg_puts("[ahci] command failed\n");
      dbg_label_hex32("[ahci] port.is=", is);
      dbg_label_hex32("[ahci] port.tfd=", tfd);
      return -1;
    }
    cpu_relax();
  }

  dbg_puts("[ahci] command timeout\n");
  dbg_label_hex32("[ahci] port.ci=", mmio_read32(&port->ci));
  dbg_label_hex32("[ahci] port.is=", mmio_read32(&port->is));
  dbg_label_hex32("[ahci] port.tfd=", mmio_read32(&port->tfd));
  return -1;
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
  dbg_puts("[ahci] identify lba28=");
  dbg_hex32(lba28);
  dbg_puts(" lba48=");
  dbg_hex64(lba48);
  dbg_putc('\n');
  ctx->lba_count = lba48 ? lba48 : (uint64_t)lba28;
  return ctx->lba_count != 0 ? 0 : -1;
}

static int ahci_read_block(void *opaque, uint32_t block_no, void *buffer) {
  struct ahci_port_ctx *ctx = (struct ahci_port_ctx *)opaque;
  if (!ctx || !buffer || (uint64_t)block_no >= ctx->lba_count) {
    return -1;
  }
  if (!ctx->io_bounce) {
    return -1;
  }
  if (ahci_exec(ctx, ATA_CMD_READ_DMA_EXT, (uint64_t)block_no, 1,
                ctx->io_bounce, 512u, 0) != 0) {
    return -1;
  }
  for (uint32_t i = 0; i < 512u; ++i) {
    ((uint8_t *)buffer)[i] = ctx->io_bounce[i];
  }
  return 0;
}

static int ahci_write_block(void *opaque, uint32_t block_no,
                            const void *buffer) {
  struct ahci_port_ctx *ctx = (struct ahci_port_ctx *)opaque;
  if (!ctx || !buffer || (uint64_t)block_no >= ctx->lba_count) {
    return -1;
  }
  if (!ctx->io_bounce) {
    return -1;
  }
  for (uint32_t i = 0; i < 512u; ++i) {
    ctx->io_bounce[i] = ((const uint8_t *)buffer)[i];
  }
  return ahci_exec(ctx, ATA_CMD_WRITE_DMA_EXT, (uint64_t)block_no, 1,
                   ctx->io_bounce, 512u, 1);
}

static struct block_device_ops g_ahci_block_ops;
static int g_ahci_block_ops_initialized = 0;

static void ahci_init_block_ops(void) {
  if (g_ahci_block_ops_initialized) {
    return;
  }
  g_ahci_block_ops.read_block = ahci_read_block;
  g_ahci_block_ops.write_block = ahci_write_block;
  g_ahci_block_ops_initialized = 1;
}

static int ahci_setup_port(struct ahci_port_ctx *ctx) {
  struct ahci_hba_port *port = NULL;

  if (!ctx || !ctx->port) {
    return -1;
  }

  port = ctx->port;
  dbg_puts("[ahci] setup port ");
  dbg_hex32(ctx->port_no);
  dbg_puts(" sig=");
  dbg_hex32(mmio_read32(&port->sig));
  dbg_puts(" ssts=");
  dbg_hex32(mmio_read32(&port->ssts));
  dbg_puts(" cmd=");
  dbg_hex32(mmio_read32(&port->cmd));
  dbg_putc('\n');
  if (ahci_port_stop(port) != 0) {
    dbg_puts("[ahci] port stop timeout\n");
    dbg_label_hex32("[ahci] port.cmd=", mmio_read32(&port->cmd));
    return -1;
  }

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
    dbg_puts("[ahci] port start timeout\n");
    dbg_label_hex32("[ahci] port.cmd=", mmio_read32(&port->cmd));
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
    dbg_puts("[ahci] identify failed\n");
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
    dbg_puts("[ahci] no SATA controller found\n");
    g_ahci_scanned = 0;
    return -1;
  }
  if (pci_dev.prog_if != 0x01u) {
    dbg_puts("[ahci] SATA controller is not AHCI\n");
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
    dbg_puts("[ahci] invalid ABAR\n");
    g_ahci_scanned = 0;
    return -1;
  }

  g_ahci_hba = (struct ahci_hba_mem *)(uintptr_t)(abar & ~0xFULL);
  mmio_write32(&g_ahci_hba->ghc, mmio_read32(&g_ahci_hba->ghc) | AHCI_GHC_AE);

  implemented = mmio_read32(&g_ahci_hba->pi);
  dbg_puts("[ahci] controller found, ABAR=");
  dbg_hex64(abar & ~0xFULL);
  dbg_puts(" PI=");
  dbg_hex32(implemented);
  dbg_putc('\n');

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
      dbg_puts("[ahci] port setup failed: ");
      dbg_hex32(port_no);
      dbg_putc('\n');
      continue;
    }
    g_ahci_count++;
    dbg_puts("[ahci] SATA device ready on port ");
    dbg_hex32(port_no);
    dbg_puts(" sectors=");
    dbg_hex64(ctx->lba_count);
    dbg_putc('\n');
    if (g_ahci_count >= AHCI_MAX_PORTS) {
      break;
    }
  }

  if (g_ahci_count <= 0) {
    g_ahci_scanned = 0;
    dbg_puts("[ahci] no SATA device ready; retry allowed\n");
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
