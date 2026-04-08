#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/capyfs.h"

struct mem_backend {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
};

static int mem_read(void *ctx, uint32_t block_no, void *buffer) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buffer || block_no >= m->block_count) {
        return -1;
    }
    memcpy(buffer, m->data + (size_t)block_no * m->block_size, m->block_size);
    return 0;
}

static int mem_write(void *ctx, uint32_t block_no, const void *buffer) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buffer || block_no >= m->block_count) {
        return -1;
    }
    memcpy(m->data + (size_t)block_no * m->block_size, buffer, m->block_size);
    return 0;
}

static const struct block_device_ops g_mem_ops = {
    .read_block = mem_read,
    .write_block = mem_write,
};

static void mem_backend_init(struct mem_backend *m, uint32_t block_count) {
    m->block_size = CAPYFS_BLOCK_SIZE;
    m->block_count = block_count;
    m->data = (uint8_t *)calloc(block_count, CAPYFS_BLOCK_SIZE);
}

static void mem_backend_free(struct mem_backend *m) {
    free(m->data);
    m->data = NULL;
}

static void bitmap_set(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

static void write_valid_minimal_capyfs(struct mem_backend *mem) {
    struct capy_super super;
    struct capy_inode_disk root;
    uint8_t *block0 = mem->data;
    uint8_t *bmap = mem->data + CAPYFS_BLOCK_SIZE;
    uint8_t *imap = mem->data + (2u * CAPYFS_BLOCK_SIZE);
    uint8_t *itable = mem->data + (3u * CAPYFS_BLOCK_SIZE);

    memset(&super, 0, sizeof(super));
    super.magic = CAPYFS_MAGIC;
    super.version = CAPYFS_VERSION;
    super.block_size = CAPYFS_BLOCK_SIZE;
    super.block_count = mem->block_count;
    super.inode_count = 64u;
    super.bmap_start = 1u;
    super.imap_start = 2u;
    super.inode_start = 3u;
    super.data_start = 5u;
    memcpy(block0, &super, sizeof(super));

    memset(bmap, 0, CAPYFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < super.data_start; ++i) {
        bitmap_set(bmap, i);
    }

    memset(imap, 0, CAPYFS_BLOCK_SIZE);
    bitmap_set(imap, 0u);

    memset(&root, 0, sizeof(root));
    root.mode = VFS_MODE_DIR;
    root.links = 1u;
    root.perm = 0755u;
    memcpy(itable, &root, sizeof(root));
}

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[capyfs_check] %s\n", msg);
        return 1;
    }
    return 0;
}

static int test_valid_minimal_volume(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct capyfs_check_report report;
    int fails = 0;

    mem_backend_init(&mem, 128u);
    write_valid_minimal_capyfs(&mem);
    dev.name = "mem-capyfs";
    dev.block_size = CAPYFS_BLOCK_SIZE;
    dev.block_count = 128u;
    dev.ctx = &mem;
    dev.ops = &g_mem_ops;

    fails += expect_true(capyfs_check(&dev, &report) == 0,
                         "capyfs_check should inspect a valid volume");
    fails += expect_true(report.result == CAPYFS_CHECK_OK,
                         "valid minimal volume should be reported as OK");
    fails += expect_true(report.reserved_blocks_expected == 5u,
                         "reserved block count should match the formatted layout");
    fails += expect_true(report.root_entries == 0u,
                         "empty root directory should report zero entries");

    mem_backend_free(&mem);
    return fails;
}

static int test_bad_superblock(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct capyfs_check_report report;
    int fails = 0;

    mem_backend_init(&mem, 128u);
    write_valid_minimal_capyfs(&mem);
    ((struct capy_super *)mem.data)->magic = 0u;
    dev.name = "mem-capyfs";
    dev.block_size = CAPYFS_BLOCK_SIZE;
    dev.block_count = 128u;
    dev.ctx = &mem;
    dev.ops = &g_mem_ops;

    fails += expect_true(capyfs_check(&dev, &report) == 0,
                         "capyfs_check should classify bad superblocks");
    fails += expect_true(report.result == CAPYFS_CHECK_BAD_SUPER,
                         "bad magic must classify as BAD_SUPER");

    mem_backend_free(&mem);
    return fails;
}

static int test_bad_root_inode_bitmap_reference(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct capyfs_check_report report;
    struct capy_inode_disk *root = NULL;
    int fails = 0;

    mem_backend_init(&mem, 128u);
    write_valid_minimal_capyfs(&mem);
    root = (struct capy_inode_disk *)(mem.data + (3u * CAPYFS_BLOCK_SIZE));
    root->size = (uint32_t)sizeof(struct capy_dirent_disk);
    root->direct[0] = 6u;
    dev.name = "mem-capyfs";
    dev.block_size = CAPYFS_BLOCK_SIZE;
    dev.block_count = 128u;
    dev.ctx = &mem;
    dev.ops = &g_mem_ops;

    fails += expect_true(capyfs_check(&dev, &report) == 0,
                         "capyfs_check should inspect inconsistent roots");
    fails += expect_true(report.result == CAPYFS_CHECK_BAD_ROOT_INODE,
                         "unmapped root block must classify as BAD_ROOT_INODE");
    fails += expect_true(report.detail_primary == 6u,
                         "bad root report should point to the offending block");

    mem_backend_free(&mem);
    return fails;
}

int run_capyfs_check_tests(void) {
    int fails = 0;
    fails += test_valid_minimal_volume();
    fails += test_bad_superblock();
    fails += test_bad_root_inode_bitmap_reference();
    if (fails == 0) {
        printf("[tests] capyfs_check OK\n");
    }
    return fails;
}
