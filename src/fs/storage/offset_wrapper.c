#include "fs/block.h"
#include "memory/kmem.h"

struct offset_ctx {
    struct block_device *lower;
    uint32_t start;
    uint32_t count;
};

static inline void dbg_putc(char ch) {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
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

static int off_read(void *ctx, uint32_t block_no, void *buffer){
    struct offset_ctx *c = (struct offset_ctx *)ctx;
    if (block_no >= c->count) return -1;
    if (block_device_read(c->lower, c->start + block_no, buffer) != 0) {
        dbg_puts("[off] read fail blk=");
        dbg_hex32(block_no);
        dbg_puts(" abs=");
        dbg_hex32(c->start + block_no);
        dbg_putc('\n');
        return -1;
    }
    return 0;
}

static int off_write(void *ctx, uint32_t block_no, const void *buffer){
    struct offset_ctx *c = (struct offset_ctx *)ctx;
    if (block_no >= c->count) return -1;
    if (block_device_write(c->lower, c->start + block_no, buffer) != 0) {
        dbg_puts("[off] write fail blk=");
        dbg_hex32(block_no);
        dbg_puts(" abs=");
        dbg_hex32(c->start + block_no);
        dbg_putc('\n');
        return -1;
    }
    return 0;
}

static struct block_device_ops off_ops;
static int off_ops_initialized = 0;

static void off_init_ops(void) {
    if (off_ops_initialized) {
        return;
    }
    off_ops.read_block = off_read;
    off_ops.write_block = off_write;
    off_ops_initialized = 1;
}

struct block_device *block_offset_wrap(struct block_device *lower, uint32_t start_lba, uint32_t lba_count){
    off_init_ops();
    if (!lower || lba_count == 0) return NULL;
    if (start_lba >= lower->block_count) return NULL;
    if (lba_count > lower->block_count - start_lba) return NULL;
    struct block_device *dev = (struct block_device *)kalloc(sizeof(struct block_device));
    struct offset_ctx *ctx = (struct offset_ctx *)kalloc(sizeof(struct offset_ctx));
    if (!dev || !ctx){ if (dev) kfree(dev); if (ctx) kfree(ctx); return NULL; }
    ctx->lower = lower;
    ctx->start = start_lba;
    ctx->count = lba_count;
    dev->name = "offset";
    dev->block_size = lower->block_size;
    dev->block_count = lba_count;
    dev->ctx = ctx;
    dev->ops = &off_ops;
    return dev;
}
