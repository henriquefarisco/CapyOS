#include "fs/ramdisk.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"

static uint8_t *storage = NULL;
static uint32_t storage_capacity_blocks = 0;
static int ramdisk_initialized = 0;
static struct block_device ramdisk_dev;
static struct block_device_ops ramdisk_ops;

static void zero_bytes(uint8_t *dst, size_t len) {
    while (dst && len--) {
        *dst++ = 0;
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
    while (dst && src && len--) {
        *dst++ = *src++;
    }
}

/* Slice 3E.4.C (2026-05-25) — local `dbg_putc`/`dbg_hex32` removed
 * in favor of `klog(KLOG_INFO, ...)` / `klog_hex(KLOG_INFO, ...)`.
 * `ramdisk_be32` (renamed from `dbg_be32`) survives as a pure
 * utility because the block-0 write audit loads the first two
 * big-endian u32 words of the buffer for the klog line. */
static uint32_t ramdisk_be32(const uint8_t *src) {
    if (!src) {
        return 0;
    }
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static int ramdisk_read(void *ctx, uint32_t block_no, void *buffer) {
    (void)ctx;
    if (!storage || block_no >= ramdisk_dev.block_count) {
        return -1;
    }
    uint8_t *dst = (uint8_t *)buffer;
    const uint8_t *src = &storage[block_no * RAMDISK_BLOCK_SIZE];
    for (uint32_t i = 0; i < RAMDISK_BLOCK_SIZE; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

static int ramdisk_write(void *ctx, uint32_t block_no, const void *buffer) {
    (void)ctx;
    if (!storage || block_no >= ramdisk_dev.block_count) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    if (block_no == 0) {
        /* Slice 3E.4.C audit: block-0 write snapshot. The chained
         * "R W0 W1\n" dbg_* sequence collapses into two structured
         * klog entries that the kernel logger persists alongside
         * other ramdisk telemetry. */
        klog_hex(KLOG_INFO, "[ramdisk] blk0 write word0=",
                 (uint64_t)ramdisk_be32(src));
        klog_hex(KLOG_INFO, "[ramdisk] blk0 write word1=",
                 (uint64_t)ramdisk_be32(src + 4));
    }
    uint8_t *dst = &storage[block_no * RAMDISK_BLOCK_SIZE];
    for (uint32_t i = 0; i < RAMDISK_BLOCK_SIZE; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

void ramdisk_init(uint32_t block_count) {
    int first_init = !ramdisk_initialized;
    uint8_t *new_storage = NULL;
    if (first_init) {
        ramdisk_ops.read_block = ramdisk_read;
        ramdisk_ops.write_block = ramdisk_write;
        ramdisk_dev.name = "ramdisk";
        ramdisk_dev.block_size = RAMDISK_BLOCK_SIZE;
        ramdisk_dev.block_count = 0;
        ramdisk_dev.ctx = NULL;
        ramdisk_dev.ops = &ramdisk_ops;
    }

    uint32_t previous_blocks = ramdisk_dev.block_count;
    if (block_count > RAMDISK_MAX_BLOCKS) {
        block_count = RAMDISK_MAX_BLOCKS;
    }

    if (block_count > storage_capacity_blocks) {
        size_t previous_bytes = (size_t)storage_capacity_blocks * RAMDISK_BLOCK_SIZE;
        size_t new_bytes = (size_t)block_count * RAMDISK_BLOCK_SIZE;
        new_storage = (uint8_t *)kalloc(new_bytes);
        if (!new_storage) {
            if (first_init) {
                ramdisk_dev.block_count = 0;
                return;
            }
            block_count = previous_blocks;
        } else {
            if (storage && previous_bytes) {
                copy_bytes(new_storage, storage, previous_bytes);
            }
            if (new_bytes > previous_bytes) {
                zero_bytes(new_storage + previous_bytes, new_bytes - previous_bytes);
            }
            if (storage) {
                kfree(storage);
            }
            storage = new_storage;
            storage_capacity_blocks = block_count;
        }
    } else if (storage && block_count > previous_blocks) {
        zero_bytes(storage + (size_t)previous_blocks * RAMDISK_BLOCK_SIZE,
                   (size_t)(block_count - previous_blocks) * RAMDISK_BLOCK_SIZE);
    } else if (first_init && storage && block_count > 0) {
        zero_bytes(storage, (size_t)block_count * RAMDISK_BLOCK_SIZE);
    }

    ramdisk_dev.block_count = storage ? block_count : 0;
    ramdisk_initialized = storage != NULL;
}

struct block_device *ramdisk_device(void) {
    if (ramdisk_dev.block_count == 0) {
        return NULL;
    }
    return &ramdisk_dev;
}

void ramdisk_fill(const void *data, size_t size) {
    size_t bytes = ramdisk_dev.block_count * (size_t)RAMDISK_BLOCK_SIZE;
    if (!storage || !data || size == 0 || bytes == 0) {
        return;
    }
    ramdisk_initialized = 1;
    if (size > bytes) {
        size = bytes;
    }
    const uint8_t *src = (const uint8_t *)data;
    for (size_t i = 0; i < size; ++i) {
        storage[i] = src[i];
    }
    for (size_t i = size; i < bytes; ++i) {
        storage[i] = 0;
    }
}
