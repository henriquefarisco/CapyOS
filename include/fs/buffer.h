#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"

#define BUFFER_CACHE_MAX 64
#define BUFFER_BLOCK_SIZE 4096

struct buffer_head {
    struct block_device *dev;
    uint32_t block_no;
    uint8_t data[BUFFER_BLOCK_SIZE];
    uint8_t dirty;
    uint8_t valid;
    uint16_t refcount;
    struct buffer_head *hash_next;
    struct buffer_head *lru_prev;
    struct buffer_head *lru_next;
};

void buffer_cache_init(void);
struct buffer_head *buffer_get(struct block_device *dev, uint32_t block_no);
void buffer_mark_dirty(struct buffer_head *bh);
int buffer_write_back(struct buffer_head *bh);
void buffer_release(struct buffer_head *bh);
void buffer_cache_sync(struct block_device *dev);

#endif
