#include "fs/capyfs.h"

#include <stddef.h>
#include <stdint.h>

static void memory_zero_local(void *dst, size_t len) {
    uint8_t *bytes = (uint8_t *)dst;
    while (len--) {
        *bytes++ = 0;
    }
}

static int name_has_terminator(const char *name, size_t cap) {
    if (!name) {
        return 0;
    }
    for (size_t i = 0; i < cap; ++i) {
        if (name[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int read_block(struct block_device *dev, uint32_t block_no,
                      uint8_t *buffer) {
    if (!dev || !buffer || block_no >= dev->block_count) {
        return -1;
    }
    return block_device_read(dev, block_no, buffer);
}

static int bitmap_test(const uint8_t *bitmap, uint32_t bit_index) {
    uint32_t byte = bit_index / 8u;
    uint32_t bit = bit_index % 8u;
    return (bitmap[byte] & (uint8_t)(1u << bit)) != 0;
}

static int bitmap_test_on_disk(struct block_device *dev, uint32_t map_start,
                               uint32_t bit_index, uint8_t *scratch,
                               int *io_error, uint32_t *source_block) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
    uint32_t map_block = map_start + (bit_index / bits_per_block);
    uint32_t rel_bit = bit_index % bits_per_block;

    if (io_error) {
        *io_error = 0;
    }
    if (source_block) {
        *source_block = map_block;
    }
    if (read_block(dev, map_block, scratch) != 0) {
        if (io_error) {
            *io_error = 1;
        }
        return 0;
    }
    return bitmap_test(scratch, rel_bit);
}

static int validate_layout(const struct capy_super *super) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
    uint32_t block_bitmap_blocks = 0;
    uint32_t inode_bitmap_blocks = 0;
    uint32_t inode_table_blocks = 0;

    if (!super || super->magic != CAPYFS_MAGIC ||
        super->version != CAPYFS_VERSION ||
        super->block_size != CAPYFS_BLOCK_SIZE || super->block_count == 0 ||
        super->inode_count == 0) {
        return -1;
    }

    block_bitmap_blocks =
        (super->block_count + bits_per_block - 1u) / bits_per_block;
    inode_bitmap_blocks =
        (super->inode_count + bits_per_block - 1u) / bits_per_block;
    inode_table_blocks =
        (super->inode_count * (uint32_t)sizeof(struct capy_inode_disk) +
         CAPYFS_BLOCK_SIZE - 1u) /
        CAPYFS_BLOCK_SIZE;

    if (super->bmap_start != 1u) {
        return -1;
    }
    if (super->imap_start != super->bmap_start + block_bitmap_blocks) {
        return -1;
    }
    if (super->inode_start != super->imap_start + inode_bitmap_blocks) {
        return -1;
    }
    if (super->data_start != super->inode_start + inode_table_blocks) {
        return -1;
    }
    if (super->data_start >= super->block_count) {
        return -1;
    }
    return 0;
}

static int read_inode(const struct capy_super *super, struct block_device *dev,
                      uint32_t ino, struct capy_inode_disk *out,
                      uint8_t *scratch) {
    uint32_t per_block = 0;
    uint32_t block_off = 0;
    uint32_t block_no = 0;
    uint32_t index = 0;

    if (!super || !dev || !out || !scratch || ino >= super->inode_count) {
        return -1;
    }

    per_block = CAPYFS_BLOCK_SIZE / (uint32_t)sizeof(struct capy_inode_disk);
    block_off = ino / per_block;
    index = ino % per_block;
    block_no = super->inode_start + block_off;
    if (read_block(dev, block_no, scratch) != 0) {
        return -1;
    }

    *out = ((const struct capy_inode_disk *)scratch)[index];
    return 0;
}

static int validate_root_blocks(const struct capy_super *super,
                                const struct capy_inode_disk *root,
                                struct block_device *dev,
                                struct capyfs_check_report *report,
                                uint8_t *scratch) {
    uint32_t indirect_capacity = CAPYFS_BLOCK_SIZE / sizeof(uint32_t);
    uint32_t max_root_blocks = 12u + indirect_capacity;
    uint32_t max_root_size = max_root_blocks * CAPYFS_BLOCK_SIZE;

    if (!super || !root || !dev || !report || !scratch) {
        return -1;
    }
    if ((root->mode & VFS_MODE_DIR) == 0 || root->links == 0 ||
        (root->size % (uint32_t)sizeof(struct capy_dirent_disk)) != 0 ||
        root->size > max_root_size) {
        report->detail_primary = root->mode;
        report->detail_secondary = root->size;
        return -1;
    }

    for (size_t i = 0; i < 12; ++i) {
        uint32_t block = root->direct[i];
        int io_error = 0;
        if (block == 0) {
            continue;
        }
        if (block < super->data_start || block >= super->block_count ||
            !bitmap_test_on_disk(dev, super->bmap_start, block, scratch,
                                 &io_error, &report->detail_secondary)) {
            report->detail_primary = block;
            if (!io_error) {
                report->detail_secondary = (uint32_t)i;
            }
            return -1;
        }
        ++report->root_referenced_blocks;
    }

    if (root->indirect != 0) {
        int io_error = 0;
        if (root->indirect < super->data_start ||
            root->indirect >= super->block_count ||
            !bitmap_test_on_disk(dev, super->bmap_start, root->indirect, scratch,
                                 &io_error, &report->detail_secondary)) {
            report->detail_primary = root->indirect;
            if (!io_error) {
                report->detail_secondary = 0xFFFFFFFFu;
            }
            return -1;
        }
        if (read_block(dev, root->indirect, scratch) != 0) {
            return -1;
        }
        ++report->root_referenced_blocks;
        for (uint32_t i = 0; i < indirect_capacity; ++i) {
            uint32_t block = ((const uint32_t *)scratch)[i];
            io_error = 0;
            if (block == 0) {
                continue;
            }
            if (block < super->data_start || block >= super->block_count ||
                !bitmap_test_on_disk(dev, super->bmap_start, block, scratch,
                                     &io_error, &report->detail_secondary)) {
                report->detail_primary = block;
                if (!io_error) {
                    report->detail_secondary = i;
                }
                return -1;
            }
            ++report->root_referenced_blocks;
        }
    }

    return 0;
}

static int validate_root_dirents(const struct capy_super *super,
                                 const struct capy_inode_disk *root,
                                 struct block_device *dev,
                                 struct capyfs_check_report *report,
                                 uint8_t *scratch) {
    uint32_t total_entries = 0;
    uint32_t entries_per_block =
        CAPYFS_BLOCK_SIZE / (uint32_t)sizeof(struct capy_dirent_disk);
    uint32_t indirect_capacity = CAPYFS_BLOCK_SIZE / sizeof(uint32_t);
    uint32_t indirect_entries[CAPYFS_BLOCK_SIZE / sizeof(uint32_t)];

    if (!super || !root || !dev || !report || !scratch) {
        return -1;
    }

    memory_zero_local(indirect_entries, sizeof(indirect_entries));
    if (root->indirect != 0) {
        if (read_block(dev, root->indirect, scratch) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < indirect_capacity; ++i) {
            indirect_entries[i] = ((const uint32_t *)scratch)[i];
        }
    }

    total_entries = root->size / (uint32_t)sizeof(struct capy_dirent_disk);
    for (uint32_t entry_index = 0; entry_index < total_entries; ++entry_index) {
        uint32_t logical_block = entry_index / entries_per_block;
        uint32_t slot = entry_index % entries_per_block;
        uint32_t block = 0;
        int io_error = 0;
        struct capy_dirent_disk entry;

        if (logical_block < 12u) {
            block = root->direct[logical_block];
        } else {
            uint32_t indirect_index = logical_block - 12u;
            if (indirect_index >= indirect_capacity) {
                report->detail_primary = logical_block;
                report->detail_secondary = entry_index;
                return -1;
            }
            block = indirect_entries[indirect_index];
        }

        if (block == 0 || block < super->data_start ||
            block >= super->block_count ||
            !bitmap_test_on_disk(dev, super->bmap_start, block, scratch,
                                 &io_error, &report->detail_secondary)) {
            report->detail_primary = block;
            if (!io_error) {
                report->detail_secondary = entry_index;
            }
            return -1;
        }
        if (read_block(dev, block, scratch) != 0) {
            return -1;
        }

        entry = ((const struct capy_dirent_disk *)scratch)[slot];
        if (entry.ino == 0) {
            continue;
        }
        if (entry.ino >= super->inode_count ||
            !bitmap_test_on_disk(dev, super->imap_start, entry.ino, scratch,
                                 &io_error, &report->detail_secondary) ||
            !name_has_terminator(entry.name, CAPYFS_NAME_MAX)) {
            report->detail_primary = entry.ino;
            if (!io_error) {
                report->detail_secondary = entry_index;
            }
            return -1;
        }
        ++report->root_entries;
    }
    return 0;
}

const char *capyfs_check_result_label(uint32_t result) {
    switch (result) {
    case CAPYFS_CHECK_OK:
        return "ok";
    case CAPYFS_CHECK_BAD_DEVICE:
        return "bad-device";
    case CAPYFS_CHECK_IO_ERROR:
        return "io-error";
    case CAPYFS_CHECK_BAD_SUPER:
        return "bad-super";
    case CAPYFS_CHECK_BAD_LAYOUT:
        return "bad-layout";
    case CAPYFS_CHECK_BAD_ROOT_INODE:
        return "bad-root";
    case CAPYFS_CHECK_BAD_BITMAP:
        return "bad-bitmap";
    case CAPYFS_CHECK_BAD_DIRENT:
        return "bad-dirent";
    default:
        return "unknown";
    }
}

int capyfs_check(struct block_device *dev, struct capyfs_check_report *out) {
    struct capyfs_check_report report;
    struct capy_inode_disk root;
    uint8_t block0[CAPYFS_BLOCK_SIZE];
    uint8_t bitmap_block[CAPYFS_BLOCK_SIZE];
    uint8_t inode_block[CAPYFS_BLOCK_SIZE];

    memory_zero_local(&report, sizeof(report));
    report.result = CAPYFS_CHECK_BAD_DEVICE;

    if (!dev || !out || dev->block_size != CAPYFS_BLOCK_SIZE ||
        dev->block_count == 0) {
        if (out) {
            *out = report;
        }
        return -1;
    }

    if (read_block(dev, 0u, block0) != 0) {
        report.result = CAPYFS_CHECK_IO_ERROR;
        *out = report;
        return 0;
    }

    report.super = *(const struct capy_super *)block0;
    if (report.super.magic != CAPYFS_MAGIC ||
        report.super.block_size != CAPYFS_BLOCK_SIZE ||
        report.super.version != CAPYFS_VERSION) {
        report.result = CAPYFS_CHECK_BAD_SUPER;
        report.detail_primary = report.super.magic;
        report.detail_secondary = report.super.version;
        *out = report;
        return 0;
    }
    if (report.super.block_count > dev->block_count ||
        validate_layout(&report.super) != 0) {
        report.result = CAPYFS_CHECK_BAD_LAYOUT;
        report.detail_primary = report.super.data_start;
        report.detail_secondary = report.super.block_count;
        *out = report;
        return 0;
    }

    report.reserved_blocks_expected = report.super.data_start;
    if (read_inode(&report.super, dev, 0u, &root, inode_block) != 0) {
        report.result = CAPYFS_CHECK_IO_ERROR;
        report.detail_primary = 0u;
        *out = report;
        return 0;
    }

    for (uint32_t block = 0; block < report.super.data_start; ++block) {
        uint32_t bitmap_block_no =
            report.super.bmap_start + (block / (CAPYFS_BLOCK_SIZE * 8u));
        if (read_block(dev, bitmap_block_no, bitmap_block) != 0) {
            report.result = CAPYFS_CHECK_IO_ERROR;
            report.detail_primary = bitmap_block_no;
            *out = report;
            return 0;
        }
        if (!bitmap_test(bitmap_block, block % (CAPYFS_BLOCK_SIZE * 8u))) {
            report.result = CAPYFS_CHECK_BAD_BITMAP;
            report.detail_primary = block;
            report.detail_secondary = bitmap_block_no;
            *out = report;
            return 0;
        }
    }

    {
        uint32_t imap_block_no = report.super.imap_start;
        if (read_block(dev, imap_block_no, bitmap_block) != 0) {
            report.result = CAPYFS_CHECK_IO_ERROR;
            report.detail_primary = imap_block_no;
            *out = report;
            return 0;
        }
        if (!bitmap_test(bitmap_block, 0u)) {
            report.result = CAPYFS_CHECK_BAD_BITMAP;
            report.detail_primary = 0u;
            report.detail_secondary = imap_block_no;
            *out = report;
            return 0;
        }
    }

    {
        uint32_t bitmap_block_no = report.super.bmap_start;
        if (read_block(dev, bitmap_block_no, bitmap_block) != 0) {
            report.result = CAPYFS_CHECK_IO_ERROR;
            report.detail_primary = bitmap_block_no;
            *out = report;
            return 0;
        }
        if (validate_root_blocks(&report.super, &root, dev, &report,
                                 inode_block) != 0) {
            report.result = CAPYFS_CHECK_BAD_ROOT_INODE;
            *out = report;
            return 0;
        }
    }

    {
        if (validate_root_dirents(&report.super, &root, dev, &report,
                                  block0) != 0) {
            report.result = CAPYFS_CHECK_BAD_DIRENT;
            *out = report;
            return 0;
        }
    }

    report.result = CAPYFS_CHECK_OK;
    *out = report;
    return 0;
}
