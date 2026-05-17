/*
 * Buffer cache pacing tests (M5.4).
 *
 * Verifies the read-ahead and write-back pacer added in
 * src/fs/cache/buffer_cache.c stay within their declared budgets, update
 * the published statistics, and degrade gracefully when given empty input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/buffer.h"

#define BC_BLOCK_SIZE 4096u   /* MUST equal BUFFER_BLOCK_SIZE */
#define BC_BLOCK_COUNT 32u

struct mem_backend {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
    uint32_t reads;
    uint32_t writes;
    int fail_writes; /* if non-zero, write_block returns -1 */
};

static int mem_read(void *ctx, uint32_t blk, void *buf) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buf || blk >= m->block_count) return -1;
    memcpy(buf, m->data + (size_t)blk * m->block_size, m->block_size);
    m->reads++;
    return 0;
}

static int mem_write(void *ctx, uint32_t blk, const void *buf) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buf || blk >= m->block_count) return -1;
    if (m->fail_writes) return -1;
    memcpy(m->data + (size_t)blk * m->block_size, buf, m->block_size);
    m->writes++;
    return 0;
}

static const struct block_device_ops g_mem_ops = {
    .read_block  = mem_read,
    .write_block = mem_write,
};

static int expect_true(int cond, const char *msg) {
    if (!cond) { fprintf(stderr, "[buffer-cache-pacing] %s\n", msg); return 1; }
    return 0;
}

static void backend_init(struct mem_backend *m, struct block_device *dev,
                         const char *name) {
    m->block_size = BC_BLOCK_SIZE;
    m->block_count = BC_BLOCK_COUNT;
    m->data = (uint8_t *)calloc(BC_BLOCK_COUNT, BC_BLOCK_SIZE);
    m->reads = 0;
    m->writes = 0;
    m->fail_writes = 0;
    dev->name = name;
    dev->block_size = BC_BLOCK_SIZE;
    dev->block_count = BC_BLOCK_COUNT;
    dev->ctx = m;
    dev->ops = &g_mem_ops;
}

static int test_readahead_loads_blocks(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;

    backend_init(&mem, &dev, "mem-readahead");
    buffer_cache_invalidate(&dev);

    struct buffer_cache_stats before;
    buffer_cache_stats_get(&before);

    uint32_t loaded = buffer_cache_readahead(&dev, 0u, 4u);
    fails += expect_true(loaded == 4u, "readahead must load 4 contiguous blocks");
    fails += expect_true(mem.reads == 4u,
                         "readahead must issue exactly 4 backend reads");

    struct buffer_cache_stats after;
    buffer_cache_stats_get(&after);
    fails += expect_true(after.readaheads >= before.readaheads + 4u,
                         "readahead counter must advance by 4");

    /* A second readahead over the same range must hit the cache, not the
     * backend. */
    uint32_t reads_before = mem.reads;
    loaded = buffer_cache_readahead(&dev, 0u, 4u);
    fails += expect_true(loaded == 4u, "warm readahead returns same count");
    fails += expect_true(mem.reads == reads_before,
                         "warm readahead must not hit backend");

    buffer_cache_invalidate(&dev);
    free(mem.data);
    return fails;
}

static int test_readahead_clamps_count(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;

    backend_init(&mem, &dev, "mem-clamp");
    buffer_cache_invalidate(&dev);

    /* Ask for 1000 blocks past the end: pacer must stop at block_count. */
    uint32_t loaded = buffer_cache_readahead(&dev, BC_BLOCK_COUNT - 2u, 1000u);
    fails += expect_true(loaded == 2u,
                         "readahead must clamp to remaining device blocks");

    /* count==0 is a no-op. */
    fails += expect_true(buffer_cache_readahead(&dev, 0u, 0u) == 0,
                         "readahead with count 0 must return 0");

    /* Block size mismatch refuses. */
    struct block_device wrong;
    wrong = dev;
    wrong.block_size = 512u;
    fails += expect_true(buffer_cache_readahead(&wrong, 0u, 4u) == 0,
                         "readahead with mismatched block_size must return 0");

    buffer_cache_invalidate(&dev);
    free(mem.data);
    return fails;
}

static int test_writeback_pass_respects_budget(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;

    backend_init(&mem, &dev, "mem-writeback");
    buffer_cache_invalidate(&dev);

    /* Dirty 5 distinct blocks. */
    for (uint32_t blk = 0; blk < 5; ++blk) {
        struct buffer_head *bh = buffer_get(&dev, blk);
        fails += expect_true(bh != NULL, "buffer_get for dirty seed");
        if (!bh) break;
        memset(bh->data, (int)(0xA0 + blk), BC_BLOCK_SIZE);
        buffer_mark_dirty(bh);
        buffer_release(bh);
    }
    fails += expect_true(buffer_cache_dirty_count(&dev) == 5u,
                         "5 dirty blocks should be tracked");

    /* Pacer must respect max_blocks: ask for 3, expect 3. */
    uint32_t writes_before = mem.writes;
    uint32_t flushed = buffer_cache_writeback_pass(&dev, 3u);
    fails += expect_true(flushed == 3u,
                         "writeback_pass must honour max_blocks budget");
    fails += expect_true(mem.writes == writes_before + 3u,
                         "exactly 3 backend writes for the paced batch");
    fails += expect_true(buffer_cache_dirty_count(&dev) == 2u,
                         "2 dirty blocks must remain after first pass");

    /* Second pass drains the rest. */
    flushed = buffer_cache_writeback_pass(&dev, 100u);
    fails += expect_true(flushed == 2u, "second pass drains remaining 2");
    fails += expect_true(buffer_cache_dirty_count(&dev) == 0u,
                         "no dirty blocks remain");

    /* No-op pass is cheap and reports 0. */
    fails += expect_true(buffer_cache_writeback_pass(&dev, 8u) == 0u,
                         "pass on clean cache returns 0");
    fails += expect_true(buffer_cache_writeback_pass(&dev, 0u) == 0u,
                         "pass with budget 0 is a no-op");

    buffer_cache_invalidate(&dev);
    free(mem.data);
    return fails;
}

static int test_writeback_pass_stops_on_error(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;

    backend_init(&mem, &dev, "mem-fail");
    buffer_cache_invalidate(&dev);

    struct buffer_head *bh = buffer_get(&dev, 0u);
    fails += expect_true(bh != NULL, "buffer_get for fail seed");
    if (bh) {
        memset(bh->data, 0xCD, BC_BLOCK_SIZE);
        buffer_mark_dirty(bh);
        buffer_release(bh);
    }

    /* Force the backend to fail. The pacer must surface the error by
     * stopping early, NOT silently report success. */
    mem.fail_writes = 1;
    uint32_t flushed = buffer_cache_writeback_pass(&dev, 4u);
    fails += expect_true(flushed == 0u,
                         "writeback_pass must stop on backend write error");
    fails += expect_true(buffer_cache_dirty_count(&dev) == 1u,
                         "dirty block remains after failed writeback");

    /* Recover: backend stops failing, next pass succeeds. */
    mem.fail_writes = 0;
    fails += expect_true(buffer_cache_writeback_pass(&dev, 4u) == 1u,
                         "recovery pass writes the remaining block");
    fails += expect_true(buffer_cache_dirty_count(&dev) == 0u,
                         "cache fully clean after recovery");

    buffer_cache_invalidate(&dev);
    free(mem.data);
    return fails;
}

int run_buffer_cache_pacing_tests(void) {
    int fails = 0;
    fails += test_readahead_loads_blocks();
    fails += test_readahead_clamps_count();
    fails += test_writeback_pass_respects_budget();
    fails += test_writeback_pass_stops_on_error();
    if (fails == 0) {
        printf("[tests] buffer_cache_pacing OK\n");
    }
    return fails;
}
