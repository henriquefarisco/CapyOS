#include "fs/block.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"

struct chunk_ctx {
    struct block_device *lower;
    uint32_t ratio;        // chunk_size / lower->block_size
    volatile int operation_busy;
};

/* Slice 3E.4.C (2026-05-25) — local `dbg_putc`/`dbg_puts`/`dbg_hex32`
 * removed; chunk read/write failure traces now go through
 * `klog_hex(KLOG_WARN, ...)`. Each failure emits three structured
 * entries (chunked block, sub-block index, absolute lower-device
 * block) so downstream parsers can correlate the chunked address
 * with the underlying lower-device address. */

static void chunk_operation_begin(struct chunk_ctx *ctx) {
    while (__sync_lock_test_and_set(&ctx->operation_busy, 1))
        __asm__ volatile("pause" ::: "memory");
}

static void chunk_operation_end(struct chunk_ctx *ctx) {
    __sync_lock_release(&ctx->operation_busy);
}

static int chunk_read(void *ctx, uint32_t block_no, void *buffer){
    struct chunk_ctx *c = (struct chunk_ctx *)ctx;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t start = block_no * c->ratio;
    int rc = 0;
    chunk_operation_begin(c);
    for (uint32_t i = 0; i < c->ratio; ++i){
        if (block_device_read(c->lower, start + i, dst + i * c->lower->block_size) != 0) {
            klog_hex(KLOG_WARN, "[chunk] read fail blk=", (uint64_t)block_no);
            klog_hex(KLOG_WARN, "[chunk] read fail sub=", (uint64_t)i);
            klog_hex(KLOG_WARN, "[chunk] read fail abs=",
                     (uint64_t)(start + i));
            rc = -1;
            break;
        }
    }
    chunk_operation_end(c);
    return rc;
}

static int chunk_write(void *ctx, uint32_t block_no, const void *buffer){
    struct chunk_ctx *c = (struct chunk_ctx *)ctx;
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t start = block_no * c->ratio;
    int rc = 0;
    chunk_operation_begin(c);
    for (uint32_t i = 0; i < c->ratio; ++i){
        if (block_device_write(c->lower, start + i, src + i * c->lower->block_size) != 0) {
            klog_hex(KLOG_WARN, "[chunk] write fail blk=", (uint64_t)block_no);
            klog_hex(KLOG_WARN, "[chunk] write fail sub=", (uint64_t)i);
            klog_hex(KLOG_WARN, "[chunk] write fail abs=",
                     (uint64_t)(start + i));
            rc = -1;
            break;
        }
    }
    chunk_operation_end(c);
    return rc;
}

static enum block_io_error_class chunk_flush_ex(void *ctx) {
    struct chunk_ctx *c = (struct chunk_ctx *)ctx;
    enum block_io_error_class cls;
    if (!c || !c->lower)
        return BLOCK_IO_ERR_PERMANENT;
    chunk_operation_begin(c);
    cls = block_device_flush_once_ex(c->lower);
    chunk_operation_end(c);
    return cls;
}

static const struct block_device_ops chunk_ops = {
    .read_block = chunk_read,
    .write_block = chunk_write,
};

static const struct block_device_ops chunk_flush_ops = {
    .read_block = chunk_read,
    .write_block = chunk_write,
    .flush_ex = chunk_flush_ex,
};

struct block_device *block_chunked_wrap(struct block_device *lower, uint32_t chunk_size){
    if (!lower || chunk_size == 0 || (chunk_size % lower->block_size) != 0) return NULL;
    uint32_t ratio = chunk_size / lower->block_size;
    if (ratio == 0 || lower->block_count < ratio) return NULL;
    struct block_device *dev = (struct block_device *)kalloc(sizeof(struct block_device));
    struct chunk_ctx *ctx = (struct chunk_ctx *)kalloc(sizeof(struct chunk_ctx));
    if (!dev || !ctx){ if (dev) kfree(dev); if (ctx) kfree(ctx); return NULL; }
    ctx->lower = lower;
    ctx->ratio = ratio;
    ctx->operation_busy = 0;
    dev->name = "chunked";
    dev->block_size = chunk_size;
    dev->block_count = lower->block_count / ctx->ratio;
    if (dev->block_count == 0){
        kfree(ctx);
        kfree(dev);
        return NULL;
    }
    dev->ctx = ctx;
    dev->ops = block_device_supports_flush(lower) ? &chunk_flush_ops : &chunk_ops;
    return dev;
}
