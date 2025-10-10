#include <stdint.h>
#include "arch/x86/hw/io.h"
#include "fs/block.h"
#include "memory/kmem.h"

// Minimal ATA PIO (primary master, LBA28)

#define ATA_IO_BASE      0x1F0
#define ATA_REG_DATA     (ATA_IO_BASE + 0)
#define ATA_REG_SECCNT   (ATA_IO_BASE + 2)
#define ATA_REG_LBA0     (ATA_IO_BASE + 3)
#define ATA_REG_LBA1     (ATA_IO_BASE + 4)
#define ATA_REG_LBA2     (ATA_IO_BASE + 5)
#define ATA_REG_DRIVE    (ATA_IO_BASE + 6)
#define ATA_REG_CMD      (ATA_IO_BASE + 7)
#define ATA_REG_STATUS   (ATA_IO_BASE + 7)

#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static void ata_io_delay(void){
    (void)inb(ATA_REG_STATUS);
    (void)inb(ATA_REG_STATUS);
    (void)inb(ATA_REG_STATUS);
    (void)inb(ATA_REG_STATUS);
}

static int ata_wait_ready(void){
    uint8_t st;
    do { st = inb(ATA_REG_STATUS); } while (st & ATA_STATUS_BSY);
    return (st & ATA_STATUS_DRDY) ? 0 : -1;
}

static int ata_wait_drq(void){
    uint8_t st;
    do { st = inb(ATA_REG_STATUS); if (st & ATA_STATUS_ERR) return -1; } while (!(st & ATA_STATUS_DRQ));
    return 0;
}

static int ata_pio_read_sector(uint32_t lba, void *buffer){
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_REG_SECCNT, 1);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_REG_CMD, ATA_CMD_READ_SECTORS);
    ata_io_delay();
    if (ata_wait_drq() != 0) return -1;
    uint16_t *dst = (uint16_t *)buffer;
    for (int i = 0; i < 256; ++i){ dst[i] = inw(ATA_REG_DATA); }
    return 0;
}

static int ata_pio_write_sector(uint32_t lba, const void *buffer){
    if (ata_wait_ready() != 0) return -1;
    outb(ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_REG_SECCNT, 1);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_REG_CMD, ATA_CMD_WRITE_SECTORS);
    ata_io_delay();
    if (ata_wait_drq() != 0) return -1;
    const uint16_t *src = (const uint16_t *)buffer;
    for (int i = 0; i < 256; ++i){ outw(ATA_REG_DATA, src[i]); }
    ata_io_delay();
    return 0;
}

// Expose as 512B block device
static int ata_read_block(void *ctx, uint32_t block_no, void *buffer){
    (void)ctx; return ata_pio_read_sector(block_no, buffer);
}
static int ata_write_block(void *ctx, uint32_t block_no, const void *buffer){
    (void)ctx; return ata_pio_write_sector(block_no, buffer);
}

static struct block_device_ops ata_ops = {
    .read_block = ata_read_block,
    .write_block = ata_write_block,
};

static struct block_device g_ata_dev;
static int g_ata_present = 0;

void ata_init(void){
    // Try to select drive and check status
    if (ata_wait_ready() != 0) { g_ata_present = 0; return; }
    g_ata_dev.name = "ata0-master";
    g_ata_dev.block_size = 512;
    g_ata_dev.block_count = 1024 * 128; // 64 MiB default guess; real size could be read via IDENTIFY
    g_ata_dev.ctx = NULL;
    g_ata_dev.ops = &ata_ops;
    g_ata_present = 1;
}

struct block_device *ata_primary_device(void){
    return g_ata_present ? &g_ata_dev : NULL;
}

