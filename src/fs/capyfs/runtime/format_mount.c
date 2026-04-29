#include "internal/capyfs_runtime_internal.h"
#include "fs/capyfs_journal_integration.h"

int capyfs_format(struct block_device *dev,
                  uint32_t inode_count,
                  uint32_t block_count,
                  capyfs_progress_cb progress) {
    uint8_t scratch_local[CAPYFS_BLOCK_SIZE];
    uint8_t *scratch = scratch_local;
    volatile uint8_t *vscratch = scratch_local;
    int scratch_heap = 0;
    if (!dev || dev->block_size != CAPYFS_BLOCK_SIZE) {
        return -10;
    }
    if (block_count == 0 || block_count > dev->block_count) {
        block_count = dev->block_count;
    }

    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8;
    if (inode_count == 0) {
        inode_count = 64;
    }
    uint32_t block_bitmap_blocks = (block_count + bits_per_block - 1) / bits_per_block;
    uint32_t inode_bitmap_blocks = (inode_count + bits_per_block - 1) / bits_per_block;
    uint32_t inode_table_blocks = (inode_count * sizeof(struct capy_inode_disk) + CAPYFS_BLOCK_SIZE - 1) / CAPYFS_BLOCK_SIZE;

    uint32_t bmap_start = 1;
    uint32_t imap_start = bmap_start + block_bitmap_blocks;
    uint32_t inode_start = imap_start + inode_bitmap_blocks;
    uint32_t data_start = inode_start + inode_table_blocks;
    if (data_start >= block_count) {
        return -11;
    }

    if (progress) {
        progress("Preparando", 0);
    }

    /* Formatting is part of the boot-critical path.  Bypass the buffer cache
     * entirely here so the initial superblock and metadata do not depend on
     * any stale cached state from previous retries or transient firmware I/O. */
    buffer_cache_invalidate(dev);
    memory_zero(scratch, CAPYFS_BLOCK_SIZE);
    store_u32_le_volatile(vscratch + 0, CAPYFS_MAGIC);
    store_u32_le_volatile(vscratch + 4, CAPYFS_VERSION);
    store_u32_le_volatile(vscratch + 8, CAPYFS_BLOCK_SIZE);
    store_u32_le_volatile(vscratch + 12, block_count);
    store_u32_le_volatile(vscratch + 16, inode_count);
    store_u32_le_volatile(vscratch + 20, bmap_start);
    store_u32_le_volatile(vscratch + 24, imap_start);
    store_u32_le_volatile(vscratch + 28, inode_start);
    store_u32_le_volatile(vscratch + 32, data_start);
    dbg_putc_serial('F');
    dbg_putc_serial(' ');
    dbg_hex32_serial(dbg_be32_serial_load(scratch));
    dbg_putc_serial(' ');
    dbg_hex32_serial(dbg_be32_serial_load(scratch + 4));
    dbg_putc_serial('\n');
    if (block_device_write(dev, 0, scratch) != 0) {
        return capyfs_format_finish(scratch, scratch_heap, -12);
    }

    if (progress) {
        progress("Superbloco", 5);
    }

    uint32_t meta_blocks = (data_start > bmap_start) ? (data_start - bmap_start) : 0;
    capyfs_dbg_puts("[CAPYFS] limpando metadados begin");
    memory_zero(scratch, CAPYFS_BLOCK_SIZE);
    for (uint32_t i = bmap_start; i < data_start; ++i) {
        if (block_device_write(dev, i, scratch) != 0) {
            return capyfs_format_finish(scratch, scratch_heap, -13);
        }

        if (progress && meta_blocks) {
            uint32_t done = (i - bmap_start) + 1;
            uint32_t pct = 5 + (done * 60) / meta_blocks;
            if (pct > 70) pct = 70;
            progress("Limpando metadados", pct);
        }
    }

    if (progress) {
        progress("Metadados", 70);
    }

    uint32_t used_blocks = data_start;
    capyfs_dbg_puts("[CAPYFS] reservando blocos begin");
    if (progress) {
        progress("Reservando blocos", 72);
    }
    for (uint32_t map = 0; map < block_bitmap_blocks; ++map) {
        uint32_t bit_start = map * bits_per_block;
        uint32_t bit_end = bit_start + bits_per_block;
        if (bit_end > used_blocks) {
            bit_end = used_blocks;
        }
        memory_zero(scratch, CAPYFS_BLOCK_SIZE);
        for (uint32_t blk = bit_start; blk < bit_end; ++blk) {
            uint32_t rel = blk - bit_start;
            uint32_t byte = rel / 8;
            uint32_t bit = rel % 8;
            scratch[byte] |= (uint8_t)(1u << bit);
        }
        if (block_device_write(dev, bmap_start + map, scratch) != 0) {
            return capyfs_format_finish(scratch, scratch_heap, -14);
        }
        if (progress && block_bitmap_blocks) {
            uint32_t pct = 70 + ((map + 1) * 15) / block_bitmap_blocks;
            if (pct > 85) pct = 85;
            progress("Reservando blocos", pct);
        }
    }

    if (progress) {
        progress("Reservando blocos", 85);
    }

    uint32_t root_ino = 0;
    memory_zero(scratch, CAPYFS_BLOCK_SIZE);
    scratch[0] |= 0x01u;
    if (block_device_write(dev, imap_start + root_ino / bits_per_block,
                           scratch) != 0) {
        return capyfs_format_finish(scratch, scratch_heap, -15);
    }

    struct capy_inode_disk root_disk;
    memory_zero(&root_disk, sizeof(root_disk));
    root_disk.mode = VFS_MODE_DIR;
    root_disk.links = 1;
    root_disk.size = 0;
    root_disk.uid = 0;
    root_disk.gid = 0;
    root_disk.perm = 0755;
    memory_zero(scratch, CAPYFS_BLOCK_SIZE);
    for (size_t i = 0; i < sizeof(root_disk); ++i) {
        scratch[i] = ((const uint8_t *)&root_disk)[i];
    }
    if (block_device_write(dev, inode_start, scratch) != 0) {
        return capyfs_format_finish(scratch, scratch_heap, -16);
    }

    if (progress) {
        progress("Criando raiz", 95);
    }
    buffer_cache_invalidate(dev);
    if (progress) {
        progress("Concluido", 100);
    }
    return capyfs_format_finish(scratch, scratch_heap, 0);
}

int mount_capyfs(struct block_device *dev, struct super_block *sb) {
    if (!dev || !sb) {
        return -10;
    }
    if (dev->block_size != CAPYFS_BLOCK_SIZE) {
        return -11;
    }

    struct buffer_head *bh = buffer_get(dev, 0);
    if (!bh) {
        return -12;
    }

    struct capy_super *disk_super = (struct capy_super *)bh->data;
    if (disk_super->magic != CAPYFS_MAGIC || disk_super->block_size != CAPYFS_BLOCK_SIZE) {
        buffer_release(bh);
        return -13;
    }

    struct capyfs_mount *mnt = (struct capyfs_mount *)kalloc(sizeof(struct capyfs_mount));
    if (!mnt) {
        buffer_release(bh);
        return -14;
    }
    mnt->dev = dev;
    mnt->super = *disk_super;
    buffer_release(bh);

    sb->bdev = dev;
    sb->fs_private = mnt;

    struct capy_inode_disk root_disk;
    if (capyfs_read_inode_disk(mnt, 0, &root_disk) != 0) {
        kfree(mnt);
        return -15;
    }

    struct inode *root_inode = capyfs_create_vfs_inode(sb, mnt, 0, &root_disk);
    if (!root_inode) {
        kfree(mnt);
        return -16;
    }

    struct dentry *root_dentry = (struct dentry *)kalloc(sizeof(struct dentry));
    if (!root_dentry) {
        kfree(root_inode->private_data);
        kfree(root_inode);
        kfree(mnt);
        return -17;
    }
    root_dentry->parent = NULL;
    root_dentry->first_child = NULL;
    root_dentry->next_sibling = NULL;
    root_dentry->inode = root_inode;
    root_dentry->refcount = 1;
    cstring_copy(root_dentry->name, VFS_NAME_MAX, "/");

    sb->root = root_dentry;

    capyfs_journal_mount_hook(dev, mnt->super.data_start,
                              &mnt->super, sizeof(mnt->super));

    return 0;
}
