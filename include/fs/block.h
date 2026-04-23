#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>
#include <stdint.h>

typedef int (*block_read_fn)(void *ctx, uint32_t block_no, void *buffer);
typedef int (*block_write_fn)(void *ctx, uint32_t block_no, const void *buffer);

struct block_device_ops {
    block_read_fn  read_block;
    block_write_fn write_block;
};

struct block_device {
    const char *name;
    uint32_t block_size;
    uint32_t block_count;
    void *ctx;                         // backend-specific pointer
    const struct block_device_ops *ops;
};

int block_device_read(struct block_device *dev, uint32_t block_no, void *buffer);
int block_device_write(struct block_device *dev, uint32_t block_no, const void *buffer);

// Creates a logical device that exposes larger blocks by chunking lower devices.
// Returns a new heap-allocated device or NULL on failure.
struct block_device *block_chunked_wrap(struct block_device *lower, uint32_t chunk_size);

// Creates a logical view over a sub-range of a lower device, starting at
// LBA 'start_lba' and exposing 'lba_count' blocks with the SAME block_size
// as the lower device.
struct block_device *block_offset_wrap(struct block_device *lower, uint32_t start_lba, uint32_t lba_count);

#endif
