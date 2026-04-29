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

struct buffer_cache_stats {
    uint32_t capacity;
    uint32_t valid;
    uint32_t dirty;
    uint32_t pinned;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writebacks;
    uint64_t read_errors;
    uint64_t write_errors;
    uint64_t readaheads;
    uint64_t writeback_passes;
};

void buffer_cache_init(void);
struct buffer_head *buffer_get(struct block_device *dev, uint32_t block_no);
void buffer_mark_dirty(struct buffer_head *bh);
int buffer_write_back(struct buffer_head *bh);
void buffer_release(struct buffer_head *bh);
int buffer_cache_sync(struct block_device *dev);
void buffer_cache_invalidate(struct block_device *dev);
void buffer_cache_stats_get(struct buffer_cache_stats *out);
int buffer_cache_last_error_block(uint32_t *out_block_no);
int buffer_cache_last_error_code(void);

/* Speculative read-ahead: brings up to count consecutive blocks starting at
 * start_block into the cache without holding any refcount. Stops early on
 * the first read error. Returns the number of blocks successfully loaded. */
uint32_t buffer_cache_readahead(struct block_device *dev,
                                uint32_t start_block, uint32_t count);

/* Background writeback pacer: flushes up to max_blocks dirty entries for the
 * given device (or all devices if dev is NULL) without removing them from the
 * cache. Stops early on the first write error. Returns the number of blocks
 * written back. Designed for cooperative pacing from idle ticks/jobs so a
 * long burst of dirty blocks does not stall input or UI when sync is
 * eventually called. */
uint32_t buffer_cache_writeback_pass(struct block_device *dev,
                                     uint32_t max_blocks);

/* Number of dirty blocks currently held for the given device, or for all
 * devices when dev is NULL. */
uint32_t buffer_cache_dirty_count(struct block_device *dev);

#endif
