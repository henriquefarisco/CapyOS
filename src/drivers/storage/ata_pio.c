// Minimal ATA PIO with primary/secondary channels and master/slave (LBA28)

#include <stdint.h>
#include "arch/x86/hw/io.h"
#include "drivers/video/vga.h"
#include "fs/block.h"
#include "memory/kmem.h"

#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_IDENTIFY       0xEC

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

#define ATA_POLL_MAX 1000000u

struct ata_ctx {
    uint16_t io_base;   // e.g., 0x1F0 or 0x170
    uint8_t drive_sel;  // 0xE0 master, 0xF0 slave (with LBA bits)
};

static inline uint16_t REG_DATA(uint16_t io){ return io + 0; }
static inline uint16_t REG_SECCNT(uint16_t io){ return io + 2; }
static inline uint16_t REG_LBA0(uint16_t io){ return io + 3; }
static inline uint16_t REG_LBA1(uint16_t io){ return io + 4; }
static inline uint16_t REG_LBA2(uint16_t io){ return io + 5; }
static inline uint16_t REG_DRIVE(uint16_t io){ return io + 6; }
static inline uint16_t REG_CMD(uint16_t io){ return io + 7; }
static inline uint16_t REG_STATUS(uint16_t io){ return io + 7; }
static inline uint16_t REG_CTRL(uint16_t io){ return (io == 0x1F0) ? 0x3F6 : 0x376; }

static void ata_log_status(const char *msg, uint16_t io, uint8_t status){
    if (!msg){
        return;
    }
    vga_write("[ata] ");
    vga_write(msg);
    vga_write(" (io=");
    char buf[7];
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = hex[(io >> 12) & 0xF];
    buf[3] = hex[(io >> 8) & 0xF];
    buf[4] = hex[(io >> 4) & 0xF];
    buf[5] = hex[io & 0xF];
    buf[6] = '\0';
    vga_write(buf);
    vga_write(" st=0x");
    char sbuf[3];
    sbuf[0] = hex[(status >> 4) & 0xF];
    sbuf[1] = hex[status & 0xF];
    sbuf[2] = '\0';
    vga_write(sbuf);
    vga_write(")\n");
}

static void ata_io_delay(uint16_t io){
    (void)inb(REG_STATUS(io));
    (void)inb(REG_STATUS(io));
    (void)inb(REG_STATUS(io));
    (void)inb(REG_STATUS(io));
}

static int ata_wait_ready(uint16_t io){
    // Wait for BSY=0. Some controllers don't assert DRDY reliably; do not fail on DRDY=0.
    for (uint32_t i = 0; i < ATA_POLL_MAX; ++i){
        uint8_t st = inb(REG_STATUS(io));
        if (!(st & ATA_STATUS_BSY)){
            return 0;
        }
    }
    ata_log_status("timeout aguardando ATA pronto", io, inb(REG_STATUS(io)));
    return -1;
}

static int ata_wait_drq(uint16_t io){
    for (uint32_t i = 0; i < ATA_POLL_MAX; ++i){
        uint8_t st = inb(REG_STATUS(io));
        if (st & ATA_STATUS_ERR){
            ata_log_status("erro aguardando DRQ", io, st);
            return -1;
        }
        if (st & ATA_STATUS_DRQ){
            return 0;
        }
    }
    ata_log_status("timeout aguardando DRQ", io, inb(REG_STATUS(io)));
    return -1;
}

static void ata_soft_reset(uint16_t io){
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

static int ata_identify(uint16_t io, uint8_t drive_sel, uint16_t *buf){
    if (ata_wait_ready(io) != 0) return -1;
    outb(REG_DRIVE(io), drive_sel);
    outb(REG_SECCNT(io), 0);
    outb(REG_LBA0(io), 0);
    outb(REG_LBA1(io), 0);
    outb(REG_LBA2(io), 0);
    outb(REG_CMD(io), ATA_CMD_IDENTIFY);
    ata_io_delay(io);
    uint8_t st = inb(REG_STATUS(io));
    if (st == 0 || st == 0xFF) return -1;
    if (ata_wait_drq(io) != 0) return -1;
    for (int i = 0; i < 256; ++i) buf[i] = inw(REG_DATA(io));
    return 0;
}

static int ata_pio_read_sector_ctx(const struct ata_ctx *ctx, uint32_t lba, void *buffer){
    uint16_t io = ctx->io_base;
    if (ata_wait_ready(io) != 0) return -1;
    outb(REG_DRIVE(io), (uint8_t)(ctx->drive_sel | ((lba >> 24) & 0x0F)));
    outb(REG_SECCNT(io), 1);
    outb(REG_LBA0(io), (uint8_t)(lba & 0xFF));
    outb(REG_LBA1(io), (uint8_t)((lba >> 8) & 0xFF));
    outb(REG_LBA2(io), (uint8_t)((lba >> 16) & 0xFF));
    outb(REG_CMD(io), ATA_CMD_READ_SECTORS);
    ata_io_delay(io);
    if (ata_wait_drq(io) != 0) return -1;
    uint16_t *dst = (uint16_t *)buffer;
    for (int i = 0; i < 256; ++i){ dst[i] = inw(REG_DATA(io)); }
    return 0;
}

static int ata_pio_write_sector_ctx(const struct ata_ctx *ctx, uint32_t lba, const void *buffer){
    uint16_t io = ctx->io_base;
    if (ata_wait_ready(io) != 0) return -1;
    outb(REG_DRIVE(io), (uint8_t)(ctx->drive_sel | ((lba >> 24) & 0x0F)));
    outb(REG_SECCNT(io), 1);
    outb(REG_LBA0(io), (uint8_t)(lba & 0xFF));
    outb(REG_LBA1(io), (uint8_t)((lba >> 8) & 0xFF));
    outb(REG_LBA2(io), (uint8_t)((lba >> 16) & 0xFF));
    outb(REG_CMD(io), ATA_CMD_WRITE_SECTORS);
    ata_io_delay(io);
    if (ata_wait_drq(io) != 0) return -1;
    const uint16_t *src = (const uint16_t *)buffer;
    for (int i = 0; i < 256; ++i){ outw(REG_DATA(io), src[i]); }
    ata_io_delay(io);
    // Wait for device to finish the write
    if (ata_wait_ready(io) != 0) return -1;
    return 0;
}

// Expose as 512B block device
static int ata_read_block(void *ctx, uint32_t block_no, void *buffer){
    return ata_pio_read_sector_ctx((const struct ata_ctx *)ctx, block_no, buffer);
}
static int ata_write_block(void *ctx, uint32_t block_no, const void *buffer){
    return ata_pio_write_sector_ctx((const struct ata_ctx *)ctx, block_no, buffer);
}

static struct block_device_ops ata_ops = {
    .read_block = ata_read_block,
    .write_block = ata_write_block,
};

#define MAX_ATA_DEV 4
static struct block_device g_ata_devs[MAX_ATA_DEV];
static struct ata_ctx g_ata_ctx[MAX_ATA_DEV];
static int g_ata_count = 0;

static const uint16_t g_channels[2] = { 0x1F0, 0x170 };
static const uint8_t g_drives[2] = { 0xE0, 0xF0 }; // master, slave (LBA)
static const char *g_names[MAX_ATA_DEV] = { "ata0-master", "ata0-slave", "ata1-master", "ata1-slave" };

void ata_init(void){
    g_ata_count = 0;
    for (int ch = 0; ch < 2 && g_ata_count < MAX_ATA_DEV; ++ch){
        uint16_t io = g_channels[ch];
        // Quick sanity check: read status
        uint8_t st = inb(REG_STATUS(io));
        (void)st;
        // Issue soft reset per channel to improve compatibility
        ata_soft_reset(io);
        for (int dr = 0; dr < 2 && g_ata_count < MAX_ATA_DEV; ++dr){
            uint8_t sel = g_drives[dr];
            uint16_t id[256];
            if (ata_identify(io, sel, id) != 0) continue;
            uint32_t lba_sectors = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
            if (lba_sectors == 0) continue;
            struct ata_ctx *ctx = &g_ata_ctx[g_ata_count];
            ctx->io_base = io; ctx->drive_sel = sel;
            struct block_device *dev = &g_ata_devs[g_ata_count];
            dev->name = g_names[ch*2 + dr];
            dev->block_size = 512;
            dev->block_count = lba_sectors;
            dev->ctx = ctx;
            dev->ops = &ata_ops;
            g_ata_count++;
        }
    }
}

int ata_devices_count(void){ return g_ata_count; }
struct block_device *ata_device_by_index(int idx){
    if (idx < 0 || idx >= g_ata_count) return NULL;
    return &g_ata_devs[idx];
}

struct block_device *ata_primary_device(void){
    return g_ata_count > 0 ? &g_ata_devs[0] : NULL;
}
