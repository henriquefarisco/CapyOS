#include "fs/ramdisk.h"

static uint8_t storage[RAMDISK_MAX_BLOCKS * RAMDISK_BLOCK_SIZE];
static uint32_t storage_blocks = 0;

static int ramdisk_read(void *ctx, uint32_t block_no, void *buffer) {
    (void)ctx;
    if (block_no >= storage_blocks) {
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
    if (block_no >= storage_blocks) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    uint8_t *dst = &storage[block_no * RAMDISK_BLOCK_SIZE];
    for (uint32_t i = 0; i < RAMDISK_BLOCK_SIZE; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

static struct block_device_ops ramdisk_ops = {
    .read_block = ramdisk_read,
    .write_block = ramdisk_write,
};

static struct block_device ramdisk_dev = {
    .name = "ramdisk",
    .block_size = RAMDISK_BLOCK_SIZE,
    .block_count = 0,
    .ctx = NULL,
    .ops = &ramdisk_ops,
};

void ramdisk_init(uint32_t block_count) {
    if (block_count > RAMDISK_MAX_BLOCKS) {
        block_count = RAMDISK_MAX_BLOCKS;
    }
    storage_blocks = block_count;
    ramdisk_dev.block_count = block_count;
    for (uint32_t i = 0; i < block_count * RAMDISK_BLOCK_SIZE; ++i) {
        storage[i] = 0;
    }
}

struct block_device *ramdisk_device(void) {
    if (storage_blocks == 0) {
        return NULL;
    }
    return &ramdisk_dev;
}

void ramdisk_fill(const void *data, size_t size) {
    size_t bytes = storage_blocks * (size_t)RAMDISK_BLOCK_SIZE;
    if (!data || size == 0 || bytes == 0) {
        return;
    }
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
