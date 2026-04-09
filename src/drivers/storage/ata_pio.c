// Minimal ATA PIO with primary/secondary channels and master/slave (LBA28)
// Enhanced logging and device detection for better diagnostics

#include "arch/x86/hw/io.h"
#include "drivers/video/vga.h"
#include "fs/block.h"
#include <stdint.h>

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_DSC 0x10
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_CORR 0x04
#define ATA_STATUS_IDX 0x02
#define ATA_STATUS_ERR 0x01

#define ATA_POLL_MAX 2000000u
#define ATA_RETRY_COUNT 3
#define ATA_RESET_DELAY 5000u

// Log levels for structured logging
#define ATA_LOG_ERROR 0
#define ATA_LOG_WARN 1
#define ATA_LOG_INFO 2
#define ATA_LOG_DEBUG 3

// Current log level (compile-time configurable)
#ifndef ATA_LOG_LEVEL
#define ATA_LOG_LEVEL ATA_LOG_INFO
#endif

enum ata_identify_result {
  ATA_IDENTIFY_OK = 0,
  ATA_IDENTIFY_NO_DEVICE = 1,
  ATA_IDENTIFY_ATAPI = 2,
  ATA_IDENTIFY_TIMEOUT = 3,
  ATA_IDENTIFY_FAILED = -1
};

struct ata_ctx {
  uint16_t io_base;  // e.g., 0x1F0 or 0x170
  uint8_t drive_sel; // 0xE0 master, 0xF0 slave (with LBA bits)
};

static inline uint16_t REG_DATA(uint16_t io) { return io + 0; }
static inline uint16_t REG_SECCNT(uint16_t io) { return io + 2; }
static inline uint16_t REG_LBA0(uint16_t io) { return io + 3; }
static inline uint16_t REG_LBA1(uint16_t io) { return io + 4; }
static inline uint16_t REG_LBA2(uint16_t io) { return io + 5; }
static inline uint16_t REG_DRIVE(uint16_t io) { return io + 6; }
static inline uint16_t REG_CMD(uint16_t io) { return io + 7; }
static inline uint16_t REG_STATUS(uint16_t io) { return io + 7; }
static inline uint16_t REG_CTRL(uint16_t io) {
  return (io == 0x1F0) ? 0x3F6 : 0x376;
}

static void ata_log(int level, const char *msg) {
  if (level > ATA_LOG_LEVEL || !msg)
    return;
  const char *prefix = "[ata] ";
  switch (level) {
  case ATA_LOG_ERROR:
    prefix = "[ata ERROR] ";
    break;
  case ATA_LOG_WARN:
    prefix = "[ata WARN] ";
    break;
  case ATA_LOG_DEBUG:
    prefix = "[ata DBG] ";
    break;
  default:
    break;
  }
  vga_write(prefix);
  vga_write(msg);
  vga_write("\n");
}

static void ata_log_hex16(const char *label, uint16_t value) {
  static const char hex[] = "0123456789ABCDEF";
  char buf[7];
  buf[0] = '0';
  buf[1] = 'x';
  buf[2] = hex[(value >> 12) & 0xF];
  buf[3] = hex[(value >> 8) & 0xF];
  buf[4] = hex[(value >> 4) & 0xF];
  buf[5] = hex[value & 0xF];
  buf[6] = '\0';
  if (label)
    vga_write(label);
  vga_write(buf);
}

static void ata_log_status(const char *msg, uint16_t io, uint8_t status) {
  if (!msg) {
    return;
  }
  vga_write("[ata] ");
  vga_write(msg);
  vga_write(" (io=");
  ata_log_hex16(NULL, io);
  vga_write(" st=0x");
  static const char hex[] = "0123456789ABCDEF";
  char sbuf[3];
  sbuf[0] = hex[(status >> 4) & 0xF];
  sbuf[1] = hex[status & 0xF];
  sbuf[2] = '\0';
  vga_write(sbuf);
  // Decode status bits for better diagnostics
  vga_write(" [");
  if (status & ATA_STATUS_BSY)
    vga_write("BSY ");
  if (status & ATA_STATUS_DRDY)
    vga_write("RDY ");
  if (status & ATA_STATUS_DF)
    vga_write("DF ");
  if (status & ATA_STATUS_DRQ)
    vga_write("DRQ ");
  if (status & ATA_STATUS_ERR)
    vga_write("ERR ");
  vga_write("])\n");
}

static void ata_io_delay(uint16_t io) {
  (void)inb(REG_STATUS(io));
  (void)inb(REG_STATUS(io));
  (void)inb(REG_STATUS(io));
  (void)inb(REG_STATUS(io));
}

static void ata_wait_400ns(uint16_t io) {
  inb(REG_STATUS(io));
  inb(REG_STATUS(io));
  inb(REG_STATUS(io));
  inb(REG_STATUS(io));
  inb(REG_STATUS(io)); // Extra read to ensure >400ns
}

static int ata_wait_ready(uint16_t io) {
  // Wait for BSY=0. Some controllers don't assert DRDY reliably; do not fail on
  // DRDY=0.
  for (uint32_t i = 0; i < ATA_POLL_MAX; ++i) {
    uint8_t st = inb(REG_STATUS(io));
    if (st == 0xFF) {
      return -1; // floating bus: nenhum dispositivo
    }
    if (!(st & ATA_STATUS_BSY)) {
      return 0;
    }
  }
  ata_log_status("timeout aguardando ATA pronto", io, inb(REG_STATUS(io)));
  return -1;
}

static int ata_wait_drq(uint16_t io) {
  for (uint32_t i = 0; i < ATA_POLL_MAX; ++i) {
    uint8_t st = inb(REG_STATUS(io));
    if (st == 0xFF) {
      return -1;
    }
    if (st & ATA_STATUS_ERR) {
      ata_log_status("erro aguardando DRQ", io, st);
      return -1;
    }
    if (st & ATA_STATUS_DRQ) {
      return 0;
    }
  }
  ata_log_status("timeout aguardando DRQ", io, inb(REG_STATUS(io)));
  return -1;
}

static void ata_soft_reset(uint16_t io) {
  uint16_t ctrl = REG_CTRL(io);
  // Assert SRST
  outb(ctrl, 0x04);
  ata_io_delay(io);
  // Deassert SRST
  outb(ctrl, 0x00);
  // wait a bit
  ata_io_delay(io);
  ata_wait_ready(io);
}

static enum ata_identify_result ata_identify(uint16_t io, uint8_t drive_sel,
                                             uint16_t *buf) {
  if (ata_wait_ready(io) != 0)
    return -1;
  /* IDENTIFY responde melhor com bit LBA desativado; forçamos drive_sel em modo
   * CHS. */
  uint8_t sel_ident = (uint8_t)(drive_sel & (uint8_t)~0x40u);
  outb(REG_DRIVE(io), sel_ident);
  ata_io_delay(io);
  if (ata_wait_ready(io) != 0)
    return -1;
  outb(REG_SECCNT(io), 0);
  outb(REG_LBA0(io), 0);
  outb(REG_LBA1(io), 0);
  outb(REG_LBA2(io), 0);
  outb(REG_CMD(io), ATA_CMD_IDENTIFY);
  ata_io_delay(io);
  uint8_t st = inb(REG_STATUS(io));
  if (st == 0 || st == 0xFF)
    return ATA_IDENTIFY_NO_DEVICE;
  // Detecta dispositivos ATAPI (ex.: CD-ROM) para evitar log de erro.
  uint8_t sig_l1 = inb(REG_LBA1(io));
  uint8_t sig_l2 = inb(REG_LBA2(io));
  if (sig_l1 == 0x14 && sig_l2 == 0xEB) {
    return ATA_IDENTIFY_ATAPI;
  }
  if (st & ATA_STATUS_ERR) {
    return ATA_IDENTIFY_FAILED;
  }
  if (ata_wait_drq(io) != 0)
    return -1;
  for (int i = 0; i < 256; ++i)
    buf[i] = inw(REG_DATA(io));
  return ATA_IDENTIFY_OK;
}

static enum ata_identify_result
ata_identify_retry(uint16_t io, uint8_t drive_sel, uint16_t *buf) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    enum ata_identify_result r = ata_identify(io, drive_sel, buf);
    if (r == ATA_IDENTIFY_OK || r == ATA_IDENTIFY_NO_DEVICE ||
        r == ATA_IDENTIFY_ATAPI) {
      return r;
    }
    ata_soft_reset(io);
  }
  return ATA_IDENTIFY_FAILED;
}

static int ata_pio_read_sector_ctx(const struct ata_ctx *ctx, uint32_t lba,
                                   void *buffer) {
  uint16_t io = ctx->io_base;
  if (ata_wait_ready(io) != 0)
    return -1;
  outb(REG_DRIVE(io), (uint8_t)(ctx->drive_sel | ((lba >> 24) & 0x0F)));
  outb(REG_SECCNT(io), 1);
  outb(REG_LBA0(io), (uint8_t)(lba & 0xFF));
  outb(REG_LBA1(io), (uint8_t)((lba >> 8) & 0xFF));
  outb(REG_LBA2(io), (uint8_t)((lba >> 16) & 0xFF));
  outb(REG_CMD(io), ATA_CMD_READ_SECTORS);
  ata_io_delay(io);
  if (ata_wait_drq(io) != 0)
    return -1;
  uint16_t *dst = (uint16_t *)buffer;
  for (int i = 0; i < 256; ++i) {
    dst[i] = inw(REG_DATA(io));
  }
  return 0;
}

static int ata_pio_write_sector_ctx(const struct ata_ctx *ctx, uint32_t lba,
                                    const void *buffer) {
  uint16_t io = ctx->io_base;
  if (ata_wait_ready(io) != 0)
    return -1;
  outb(REG_DRIVE(io), (uint8_t)(ctx->drive_sel | ((lba >> 24) & 0x0F)));
  outb(REG_SECCNT(io), 1);
  outb(REG_LBA0(io), (uint8_t)(lba & 0xFF));
  outb(REG_LBA1(io), (uint8_t)((lba >> 8) & 0xFF));
  outb(REG_LBA2(io), (uint8_t)((lba >> 16) & 0xFF));
  outb(REG_CMD(io), ATA_CMD_WRITE_SECTORS);
  ata_io_delay(io);
  if (ata_wait_drq(io) != 0)
    return -1;
  const uint16_t *src = (const uint16_t *)buffer;
  for (int i = 0; i < 256; ++i) {
    outw(REG_DATA(io), src[i]);
  }
  ata_io_delay(io);
  // Wait for device to finish the write
  if (ata_wait_ready(io) != 0)
    return -1;
  return 0;
}

// Expose as 512B block device
static int ata_read_block(void *ctx, uint32_t block_no, void *buffer) {
  return ata_pio_read_sector_ctx((const struct ata_ctx *)ctx, block_no, buffer);
}
static int ata_write_block(void *ctx, uint32_t block_no, const void *buffer) {
  return ata_pio_write_sector_ctx((const struct ata_ctx *)ctx, block_no,
                                  buffer);
}

static struct block_device_ops ata_ops;
static int ata_ops_initialized = 0;

static void ata_init_ops(void) {
  if (ata_ops_initialized) {
    return;
  }
  ata_ops.read_block = ata_read_block;
  ata_ops.write_block = ata_write_block;
  ata_ops_initialized = 1;
}

#define MAX_ATA_DEV 4
static struct block_device g_ata_devs[MAX_ATA_DEV];
static struct ata_ctx g_ata_ctx[MAX_ATA_DEV];
static int g_ata_count = 0;

static const uint16_t g_channels[2] = {0x1F0, 0x170};
static const uint8_t g_drives[2] = {0xE0, 0xF0}; // master, slave (LBA)
static const char *g_names[MAX_ATA_DEV] = {"ata0-master", "ata0-slave",
                                           "ata1-master", "ata1-slave"};

// Returns: 0 = no device, 1 = ATA device, 2 = ATAPI device
static int ata_drive_present(uint16_t io, uint8_t drive_sel) {
  outb(REG_DRIVE(io), drive_sel);
  ata_wait_400ns(io);

  // Check if Status is floating (0xFF) or empty (0x00)
  uint8_t st = inb(REG_STATUS(io));
  if (st == 0xFF) {
    ata_log(ATA_LOG_DEBUG, "canal flutuante (0xFF) - sem dispositivo");
    return 0;
  }
  if (st == 0x00) {
    // Some devices start with 0x00, try ghost check first
    ata_log(ATA_LOG_DEBUG, "status 0x00 - verificando ghost");
  }

  // Ghost Check: Write a pattern to Sector Count and read it back.
  outb(REG_SECCNT(io), 0x55);
  outb(REG_LBA0(io), 0xAA);
  ata_wait_400ns(io);

  uint8_t seccnt_read = inb(REG_SECCNT(io));
  uint8_t lba0_read = inb(REG_LBA0(io));

  if (seccnt_read != 0x55 || lba0_read != 0xAA) {
    ata_log(ATA_LOG_DEBUG, "ghost check falhou - sem dispositivo real");
    return 0;
  }

  // Reset registers to 0 for signature probe
  outb(REG_SECCNT(io), 0);
  outb(REG_LBA0(io), 0);
  ata_wait_400ns(io);

  uint8_t sig_l1 = inb(REG_LBA1(io));
  uint8_t sig_l2 = inb(REG_LBA2(io));

  // Check for ATAPI signature (0x14, 0xEB)
  if (sig_l1 == 0x14 && sig_l2 == 0xEB) {
    ata_log(ATA_LOG_INFO, "dispositivo ATAPI detectado (CD-ROM)");
    return 2; // ATAPI
  }

  // Check for SATA bridge signature (0x69, 0x96) - rare
  if (sig_l1 == 0x69 && sig_l2 == 0x96) {
    ata_log(ATA_LOG_INFO, "assinatura SATAPI (0x69/0x96)");
    return 2; // Treat like ATAPI
  }

  // Check for floating bus signature
  if (sig_l1 == 0xFF && sig_l2 == 0xFF) {
    ata_log(ATA_LOG_DEBUG, "assinatura flutuante (0xFF/0xFF)");
    return 0;
  }

  // ATA device should have signature (0x00, 0x00) or similar
  // If we passed ghost check and don't have ATAPI sig, assume ATA
  ata_log(ATA_LOG_DEBUG, "dispositivo ATA detectado");
  return 1; // ATA
}

void ata_init(void) {
  ata_init_ops();
  ata_log(ATA_LOG_INFO, "inicializando controlador ATA PIO");
  g_ata_count = 0;

  for (int ch = 0; ch < 2 && g_ata_count < MAX_ATA_DEV; ++ch) {
    uint16_t io = g_channels[ch];

    // Check if channel exists by reading status
    uint8_t st = inb(REG_STATUS(io));
    if (st == 0xFF) {
      // Floating bus - no controller on this channel
      continue;
    }

    // Issue soft reset per channel to improve compatibility
    ata_soft_reset(io);

    for (int dr = 0; dr < 2 && g_ata_count < MAX_ATA_DEV; ++dr) {
      uint8_t sel = g_drives[dr];
      const char *dev_name = g_names[ch * 2 + dr];

      int present = ata_drive_present(io, sel);
      if (present == 0) {
        // No device on this position
        continue;
      }
      if (present == 2) {
        // ATAPI device (CD-ROM) - skip silently
        vga_write("[ata] ");
        vga_write(dev_name);
        vga_write(": ATAPI (ignorado)\n");
        continue;
      }

      // ATA device - try IDENTIFY
      uint16_t id[256];
      enum ata_identify_result r = ata_identify_retry(io, sel, id);

      if (r == ATA_IDENTIFY_NO_DEVICE) {
        ata_log(ATA_LOG_DEBUG, "IDENTIFY retornou sem dispositivo");
        continue;
      }
      if (r == ATA_IDENTIFY_ATAPI) {
        // This shouldn't happen since we check signature above
        vga_write("[ata] ");
        vga_write(dev_name);
        vga_write(": ATAPI detectado via IDENTIFY\n");
        continue;
      }
      if (r != ATA_IDENTIFY_OK) {
        vga_write("[ata] ");
        vga_write(dev_name);
        vga_write(": ");
        ata_log_status("IDENTIFY falhou", io, inb(REG_STATUS(io)));
        continue;
      }

      // Extract LBA sector count from IDENTIFY data
      uint32_t lba_sectors = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
      if (lba_sectors == 0) {
        ata_log(ATA_LOG_WARN, "dispositivo com 0 setores LBA - ignorado");
        continue;
      }

      // Register the device
      struct ata_ctx *ctx = &g_ata_ctx[g_ata_count];
      ctx->io_base = io;
      ctx->drive_sel = sel;

      struct block_device *dev = &g_ata_devs[g_ata_count];
      dev->name = dev_name;
      dev->block_size = 512;
      dev->block_count = lba_sectors;
      dev->ctx = ctx;
      dev->ops = &ata_ops;
      g_ata_count++;

      // Log device info with size
      vga_write("[ata] detectado: ");
      vga_write(dev_name);
      vga_write(" (");
      // Print size in MB (lba_sectors * 512 / 1024 / 1024)
      uint32_t size_mb = lba_sectors / 2048;
      char size_buf[16];
      size_t si = 0;
      uint32_t tmp = size_mb;
      if (tmp == 0) {
        size_buf[si++] = '0';
      } else {
        char rev[16];
        size_t ri = 0;
        while (tmp > 0) {
          rev[ri++] = '0' + (tmp % 10);
          tmp /= 10;
        }
        while (ri > 0) {
          size_buf[si++] = rev[--ri];
        }
      }
      size_buf[si] = '\0';
      vga_write(size_buf);
      vga_write(" MiB)\n");
    }
  }

  // Summary
  if (g_ata_count == 0) {
    ata_log(ATA_LOG_WARN, "nenhum dispositivo ATA detectado");
  } else {
    vga_write("[ata] total: ");
    char count_buf[4];
    count_buf[0] = '0' + (char)(g_ata_count % 10);
    count_buf[1] = '\0';
    vga_write(count_buf);
    vga_write(" dispositivo(s)\n");
  }
}

int ata_devices_count(void) { return g_ata_count; }
struct block_device *ata_device_by_index(int idx) {
  if (idx < 0 || idx >= g_ata_count)
    return NULL;
  return &g_ata_devs[idx];
}

struct block_device *ata_primary_device(void) {
  return g_ata_count > 0 ? &g_ata_devs[0] : NULL;
}
