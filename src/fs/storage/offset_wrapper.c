#include "fs/block.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"

struct offset_ctx {
    struct block_device *lower;
    uint32_t start;
    uint32_t count;
};

/* Slice 3E.4.C (2026-05-25) — local `dbg_putc`/`dbg_puts`/`dbg_hex32`
 * removed; the read/write failure traces now go through
 * `klog_hex(KLOG_WARN, ...)`. Each emit becomes two structured
 * entries (relative block + absolute block) so downstream parsers
 * can correlate offsets without parsing a single chained line. */

static int off_read(void *ctx, uint32_t block_no, void *buffer){
    struct offset_ctx *c = (struct offset_ctx *)ctx;
    if (block_no >= c->count) return -1;
    if (block_device_read(c->lower, c->start + block_no, buffer) != 0) {
        klog_hex(KLOG_WARN, "[off] read fail blk=", (uint64_t)block_no);
        klog_hex(KLOG_WARN, "[off] read fail abs=",
                 (uint64_t)(c->start + block_no));
        return -1;
    }
    return 0;
}

static int off_write(void *ctx, uint32_t block_no, const void *buffer){
    struct offset_ctx *c = (struct offset_ctx *)ctx;
    if (block_no >= c->count) return -1;
    if (block_device_write(c->lower, c->start + block_no, buffer) != 0) {
        klog_hex(KLOG_WARN, "[off] write fail blk=", (uint64_t)block_no);
        klog_hex(KLOG_WARN, "[off] write fail abs=",
                 (uint64_t)(c->start + block_no));
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
