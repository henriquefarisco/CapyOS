#include "buffer.h"

#ifndef BUFFER_HASH_SIZE
#define BUFFER_HASH_SIZE 64
#endif

static struct buffer_head buffer_cache[BUFFER_CACHE_MAX];
static struct buffer_head *hash_table[BUFFER_HASH_SIZE];
static struct buffer_head *lru_head;
static struct buffer_head *lru_tail;
static int cache_initialized = 0;

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
        bh->refcount++;
        lru_remove(bh);
        return bh;
    }

    bh = buffer_evict();
    if (!bh) {
        return NULL;
    }

    bh->dev = dev;
    bh->block_no = block_no;
    bh->refcount = 1;

    if (block_device_read(dev, block_no, bh->data) != 0) {
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
    if (block_device_write(bh->dev, bh->block_no, bh->data) != 0) {
        return -1;
    }
    bh->dirty = 0;
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

void buffer_cache_sync(struct block_device *dev) {
    for (size_t i = 0; i < BUFFER_CACHE_MAX; ++i) {
        struct buffer_head *bh = &buffer_cache[i];
        if (bh->dev == dev) {
            if (bh->dirty && bh->valid) {
                buffer_write_back(bh);
            }
        }
    }
}
