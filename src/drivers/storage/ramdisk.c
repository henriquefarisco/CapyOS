#include "fs/ramdisk.h"

static uint8_t storage[RAMDISK_MAX_BLOCKS * RAMDISK_BLOCK_SIZE];
static int ramdisk_initialized = 0;
static struct block_device ramdisk_dev;
static struct block_device_ops ramdisk_ops;

static int ramdisk_read(void *ctx, uint32_t block_no, void *buffer) {
    (void)ctx;
    if (block_no >= ramdisk_dev.block_count) {
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
    if (block_no >= ramdisk_dev.block_count) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    uint8_t *dst = &storage[block_no * RAMDISK_BLOCK_SIZE];
    for (uint32_t i = 0; i < RAMDISK_BLOCK_SIZE; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

void ramdisk_init(uint32_t block_count) {
    int first_init = !ramdisk_initialized;
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
    ramdisk_dev.block_count = block_count;
    if (first_init) {
        for (uint32_t i = 0; i < block_count * RAMDISK_BLOCK_SIZE; ++i) {
            storage[i] = 0;
        }
        ramdisk_initialized = 1;
    } else if (block_count > previous_blocks) {
        for (uint32_t i = previous_blocks * RAMDISK_BLOCK_SIZE;
             i < block_count * RAMDISK_BLOCK_SIZE; ++i) {
            storage[i] = 0;
        }
    }
}

struct block_device *ramdisk_device(void) {
    if (ramdisk_dev.block_count == 0) {
        return NULL;
    }
    return &ramdisk_dev;
}

void ramdisk_fill(const void *data, size_t size) {
    size_t bytes = ramdisk_dev.block_count * (size_t)RAMDISK_BLOCK_SIZE;
    if (!data || size == 0 || bytes == 0) {
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
