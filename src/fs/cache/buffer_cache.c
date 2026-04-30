#include "fs/buffer.h"

#ifndef BUFFER_HASH_SIZE
#define BUFFER_HASH_SIZE 256
#endif

static struct buffer_head buffer_cache[BUFFER_CACHE_MAX];
static struct buffer_head *hash_table[BUFFER_HASH_SIZE];
static struct buffer_head *lru_head;
static struct buffer_head *lru_tail;
static int cache_initialized = 0;
static uint32_t g_last_error_block_no = 0;
static int g_last_error_valid = 0;
static int g_last_error_code = 0;
static struct buffer_cache_stats g_buffer_cache_stats;

static inline void dbg_putc(char ch) {
#if defined(UNIT_TEST) || !defined(__x86_64__)
    /* Host unit tests do not have port 0xE9; the debug write is a
     * no-op so the trace path can still be exercised. */
    (void)ch;
#else
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
#endif
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

static inline uint32_t buffer_hash(struct block_device *dev, uint32_t block_no) {
    uintptr_t key = (uintptr_t)dev;
    key ^= block_no * 2654435761u;
    return (uint32_t)(key % BUFFER_HASH_SIZE);
}

static void lru_remove(struct buffer_head *bh) {
    if (!bh) return;
    if (bh->lru_prev) {
        bh->lru_prev->lru_next = bh->lru_next;
    } else {
        lru_head = bh->lru_next;
    }
    if (bh->lru_next) {
        bh->lru_next->lru_prev = bh->lru_prev;
    } else {
        lru_tail = bh->lru_prev;
    }
    bh->lru_prev = bh->lru_next = NULL;
}

static void lru_insert_front(struct buffer_head *bh) {
    bh->lru_prev = NULL;
    bh->lru_next = lru_head;
    if (lru_head) {
        lru_head->lru_prev = bh;
    } else {
        lru_tail = bh;
    }
    lru_head = bh;
}

static struct buffer_head *hash_lookup(struct block_device *dev, uint32_t block_no) {
    uint32_t index = buffer_hash(dev, block_no);
    struct buffer_head *bh = hash_table[index];
    while (bh) {
        if (bh->dev == dev && bh->block_no == block_no) {
            return bh;
        }
        bh = bh->hash_next;
    }
    return NULL;
}

static void hash_remove(struct buffer_head *bh) {
    if (!bh) return;
    uint32_t index = buffer_hash(bh->dev, bh->block_no);
    struct buffer_head **link = &hash_table[index];
    while (*link) {
        if (*link == bh) {
            *link = bh->hash_next;
            bh->hash_next = NULL;
            return;
        }
        link = &(*link)->hash_next;
    }
}

static void hash_insert(struct buffer_head *bh) {
    uint32_t index = buffer_hash(bh->dev, bh->block_no);
    bh->hash_next = hash_table[index];
    hash_table[index] = bh;
}

static struct buffer_head *buffer_evict(void) {
    struct buffer_head *bh = lru_tail;
    while (bh && bh->refcount) {
        bh = bh->lru_prev;
    }
    if (!bh) {
        return NULL;
    }
    lru_remove(bh);
    if (bh->dirty && bh->valid) {
        buffer_write_back(bh);
    }
    if (bh->valid || bh->dev) {
        g_buffer_cache_stats.evictions++;
    }
    hash_remove(bh);
    bh->dev = NULL;
    bh->block_no = 0;
    bh->dirty = 0;
    bh->valid = 0;
    return bh;
}

void buffer_cache_init(void) {
    if (cache_initialized) {
        return;
    }
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        struct buffer_head *bh = &buffer_cache[i];
        bh->dev = NULL;
        bh->block_no = 0;
        bh->dirty = 0;
        bh->valid = 0;
        bh->refcount = 0;
        bh->hash_next = NULL;
        bh->lru_prev = NULL;
        bh->lru_next = NULL;
        lru_insert_front(bh);
    }
    for (size_t i = 0; i < BUFFER_HASH_SIZE; ++i) {
        hash_table[i] = NULL;
    }
    g_buffer_cache_stats.capacity = BUFFER_CACHE_MAX;
    g_buffer_cache_stats.valid = 0;
    g_buffer_cache_stats.dirty = 0;
    g_buffer_cache_stats.pinned = 0;
    g_buffer_cache_stats.hits = 0;
    g_buffer_cache_stats.misses = 0;
    g_buffer_cache_stats.evictions = 0;
    g_buffer_cache_stats.writebacks = 0;
    g_buffer_cache_stats.read_errors = 0;
    g_buffer_cache_stats.write_errors = 0;
    g_buffer_cache_stats.readaheads = 0;
    g_buffer_cache_stats.writeback_passes = 0;
    cache_initialized = 1;
}

struct buffer_head *buffer_get(struct block_device *dev, uint32_t block_no) {
    if (!cache_initialized) {
        buffer_cache_init();
    }
    if (!dev || dev->block_size != BUFFER_BLOCK_SIZE) {
        return NULL;
    }

    struct buffer_head *bh = hash_lookup(dev, block_no);
    if (bh) {
        g_buffer_cache_stats.hits++;
        if (bh->refcount == 0) {
            lru_remove(bh);
        }
        bh->refcount++;
        return bh;
    }
    g_buffer_cache_stats.misses++;

    bh = buffer_evict();
    if (!bh) {
        dbg_puts("[buf] evict none\n");
        return NULL;
    }

    bh->dev = dev;
    bh->block_no = block_no;
    bh->refcount = 1;

    if (block_device_read(dev, block_no, bh->data) != 0) {
        g_buffer_cache_stats.read_errors++;
        dbg_puts("[buf] read fail blk=");
        dbg_hex32(block_no);
        dbg_puts(" bsz=");
        dbg_hex32(dev->block_size);
        dbg_puts(" bcnt=");
        dbg_hex32(dev->block_count);
        dbg_putc('\n');
        bh->dev = NULL;
        bh->refcount = 0;
        lru_insert_front(bh);
        return NULL;
    }

    bh->valid = 1;
    bh->dirty = 0;
    hash_insert(bh);
    return bh;
}

void buffer_mark_dirty(struct buffer_head *bh) {
    if (bh) {
        bh->dirty = 1;
    }
}

int buffer_write_back(struct buffer_head *bh) {
    if (!bh || !bh->dev || !bh->dirty || !bh->valid) {
        return 0;
    }
    int rc = block_device_write(bh->dev, bh->block_no, bh->data);
    if (rc != 0) {
        g_buffer_cache_stats.write_errors++;
        return rc;
    }
    bh->dirty = 0;
    g_buffer_cache_stats.writebacks++;
    return 0;
}

void buffer_release(struct buffer_head *bh) {
    if (!bh || !bh->refcount) {
        return;
    }
    bh->refcount--;
    if (bh->refcount == 0) {
        lru_insert_front(bh);
    }
}

int buffer_cache_sync(struct block_device *dev) {
    g_last_error_valid = 0;
    g_last_error_block_no = 0;
    g_last_error_code = 0;
    int rc = 0;
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        struct buffer_head *bh = &buffer_cache[i];
        if (bh->dev == dev) {
            if (bh->dirty && bh->valid) {
                int wrc = buffer_write_back(bh);
                if (wrc != 0) {
                    rc = -1;
                    if (!g_last_error_valid) {
                        g_last_error_valid = 1;
                        g_last_error_block_no = bh->block_no;
                        g_last_error_code = wrc;
                    }
                }
            }
        }
    }
    return rc;
}

void buffer_cache_invalidate(struct block_device *dev) {
    if (!dev) {
        return;
    }
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        struct buffer_head *bh = &buffer_cache[i];
        if (bh->dev != dev) {
            continue;
        }
        hash_remove(bh);
        if (bh->refcount == 0) {
            lru_remove(bh);
        }
        bh->dev = NULL;
        bh->block_no = 0;
        bh->dirty = 0;
        bh->valid = 0;
        bh->hash_next = NULL;
        if (bh->refcount == 0) {
            lru_insert_front(bh);
        }
    }
}

void buffer_cache_stats_get(struct buffer_cache_stats *out) {
    if (!out) {
        return;
    }
    if (!cache_initialized) {
        buffer_cache_init();
    }
    *out = g_buffer_cache_stats;
    out->capacity = BUFFER_CACHE_MAX;
    out->valid = 0;
    out->dirty = 0;
    out->pinned = 0;
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        const struct buffer_head *bh = &buffer_cache[i];
        if (bh->valid) {
            out->valid++;
        }
        if (bh->dirty) {
            out->dirty++;
        }
        if (bh->refcount != 0) {
            out->pinned++;
        }
    }
}

int buffer_cache_last_error_block(uint32_t *out_block_no) {
    if (!out_block_no || !g_last_error_valid) {
        return -1;
    }
    *out_block_no = g_last_error_block_no;
    return 0;
}

int buffer_cache_last_error_code(void) { return g_last_error_code; }

uint32_t buffer_cache_readahead(struct block_device *dev,
                                uint32_t start_block, uint32_t count) {
    if (!cache_initialized) {
        buffer_cache_init();
    }
    if (!dev || dev->block_size != BUFFER_BLOCK_SIZE || count == 0) {
        return 0;
    }
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (start_block + i < start_block) {
            break; /* overflow guard */
        }
        if (dev->block_count && (start_block + i) >= dev->block_count) {
            break;
        }
        struct buffer_head *bh = buffer_get(dev, start_block + i);
        if (!bh) {
            break;
        }
        buffer_release(bh);
        loaded++;
    }
    g_buffer_cache_stats.readaheads += loaded;
    return loaded;
}

uint32_t buffer_cache_writeback_pass(struct block_device *dev,
                                     uint32_t max_blocks) {
    if (!cache_initialized) {
        buffer_cache_init();
    }
    if (max_blocks == 0) {
        return 0;
    }
    uint32_t written = 0;
    /* Walk LRU from tail (least recently used) so frequently re-touched
     * blocks have a chance to coalesce more writes before being flushed. */
    struct buffer_head *bh = lru_tail;
    while (bh && written < max_blocks) {
        struct buffer_head *prev = bh->lru_prev;
        if (bh->valid && bh->dirty && (!dev || bh->dev == dev)) {
            int rc = buffer_write_back(bh);
            if (rc != 0) {
                /* error already counted in stats by buffer_write_back */
                break;
            }
            written++;
        }
        bh = prev;
    }
    if (written > 0) {
        g_buffer_cache_stats.writeback_passes++;
    }
    return written;
}

uint32_t buffer_cache_dirty_count(struct block_device *dev) {
    if (!cache_initialized) {
        buffer_cache_init();
    }
    uint32_t count = 0;
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        const struct buffer_head *bh = &buffer_cache[i];
        if (!bh->valid || !bh->dirty) continue;
        if (dev && bh->dev != dev) continue;
        count++;
    }
    return count;
}
