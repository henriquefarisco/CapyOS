static int capyfs_read_inode_disk(struct capyfs_mount *mnt, uint32_t ino, struct capy_inode_disk *out) {
    if (!mnt || !out || ino >= mnt->super.inode_count) {
        return -1;
    }
    uint32_t per_block = CAPYFS_BLOCK_SIZE / sizeof(struct capy_inode_disk);
    uint32_t block_off = ino / per_block;
    uint32_t index = ino % per_block;
    uint32_t block_no = mnt->super.inode_start + block_off;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct capy_inode_disk *base = (struct capy_inode_disk *)bh->data;
    *out = base[index];
    buffer_release(bh);
    return 0;
}

static int capyfs_write_inode_disk(struct capyfs_mount *mnt, uint32_t ino, const struct capy_inode_disk *src) {
    if (!mnt || !src || ino >= mnt->super.inode_count) {
        return -1;
    }
    uint32_t per_block = CAPYFS_BLOCK_SIZE / sizeof(struct capy_inode_disk);
    uint32_t block_off = ino / per_block;
    uint32_t index = ino % per_block;
    uint32_t block_no = mnt->super.inode_start + block_off;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct capy_inode_disk *base = (struct capy_inode_disk *)bh->data;
    base[index] = *src;
    buffer_mark_dirty(bh);
    buffer_release(bh);
    buffer_cache_sync(mnt->dev);
    return 0;
}

static struct inode *capyfs_create_vfs_inode(struct super_block *sb, struct capyfs_mount *mnt, uint32_t ino, const struct capy_inode_disk *disk) {
    struct inode *inode = (struct inode *)kalloc(sizeof(struct inode));
    if (!inode) {
        return NULL;
    }
    struct capyfs_inode *priv = (struct capyfs_inode *)kalloc(sizeof(struct capyfs_inode));
    if (!priv) {
        kfree(inode);
        return NULL;
    }
    priv->mount = mnt;
    priv->ino = ino;
    inode->sb = sb;
    inode->ino = ino;
    inode->mode = disk->mode;
    inode->size = disk->size;
    inode->uid = disk->uid;
    inode->gid = disk->gid;
    inode->perm = disk->perm;
    inode->ops = capyfs_file_ops();
    inode->private_data = priv;
    return inode;
}

static int capyfs_alloc_inode(struct capyfs_mount *mnt, uint32_t *out_ino) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8;
    for (uint32_t ino = 0; ino < mnt->super.inode_count; ++ino) {
        uint32_t block = mnt->super.imap_start + ino / bits_per_block;
        uint32_t rel = ino % bits_per_block;
        uint32_t byte = rel / 8;
        uint32_t bit = rel % 8;
        struct buffer_head *bh = buffer_get(mnt->dev, block);
        if (!bh) {
            return -1;
        }
        uint8_t value = bh->data[byte];
        if (!(value & (1u << bit))) {
            bh->data[byte] = value | (1u << bit);
            buffer_mark_dirty(bh);
            buffer_release(bh);
            *out_ino = ino;
            return 0;
        }
        buffer_release(bh);
    }
    return -1;
}


static void capyfs_free_inode_bit(struct capyfs_mount *mnt, uint32_t ino) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8;
    if (ino >= mnt->super.inode_count) {
        return;
    }
    uint32_t block = mnt->super.imap_start + ino / bits_per_block;
    uint32_t rel = ino % bits_per_block;
    uint32_t byte = rel / 8;
    uint32_t bit = rel % 8;
    struct buffer_head *bh = buffer_get(mnt->dev, block);
    if (!bh) {
        return;
    }
    bh->data[byte] &= (uint8_t)~(1u << bit);
    buffer_mark_dirty(bh);
    buffer_release(bh);
}

static void capyfs_free_block(struct capyfs_mount *mnt, uint32_t block) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8;
    if (!mnt || block < mnt->super.data_start || block >= mnt->super.block_count) {
        return;
    }
    uint32_t map_block = mnt->super.bmap_start + block / bits_per_block;
    uint32_t rel = block % bits_per_block;
    uint32_t byte = rel / 8;
    uint32_t bit = rel % 8;
    struct buffer_head *bh = buffer_get(mnt->dev, map_block);
    if (!bh) {
        return;
    }
    bh->data[byte] &= (uint8_t)~(1u << bit);
    buffer_mark_dirty(bh);
    buffer_release(bh);
}

static uint32_t g_next_fit_hint = 0;

static int capyfs_try_alloc_range(struct capyfs_mount *mnt, uint32_t start,
                                  uint32_t end, uint32_t *out_block) {
    uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8;
    for (uint32_t blk = start; blk < end; ++blk) {
        uint32_t block = mnt->super.bmap_start + blk / bits_per_block;
        uint32_t rel = blk % bits_per_block;
        uint32_t byte = rel / 8;
        uint32_t bit = rel % 8;
        struct buffer_head *bh = buffer_get(mnt->dev, block);
        if (!bh) {
            return -1;
        }
        uint8_t value = bh->data[byte];
        if (!(value & (1u << bit))) {
            bh->data[byte] = value | (1u << bit);
            buffer_mark_dirty(bh);
            buffer_release(bh);
            struct buffer_head *data_bh = buffer_get(mnt->dev, blk);
            if (!data_bh) {
                return -1;
            }
            zero_block(data_bh);
            buffer_release(data_bh);
            *out_block = blk;
            g_next_fit_hint = blk + 1;
            return 0;
        }
        buffer_release(bh);
    }
    return -1;
}

static int capyfs_alloc_block(struct capyfs_mount *mnt, uint32_t *out_block) {
    uint32_t ds = mnt->super.data_start;
    uint32_t bc = mnt->super.block_count;
    uint32_t hint = g_next_fit_hint;
    if (hint < ds || hint >= bc) hint = ds;
    if (capyfs_try_alloc_range(mnt, hint, bc, out_block) == 0) return 0;
    if (hint > ds) return capyfs_try_alloc_range(mnt, ds, hint, out_block);
    return -1;
}

static void capyfs_free_data_blocks(struct capyfs_mount *mnt, struct capy_inode_disk *inode_disk) {
    if (!mnt || !inode_disk) {
        return;
    }
    for (size_t i = 0; i < 12; ++i) {
        uint32_t blk = inode_disk->direct[i];
        if (blk) {
            capyfs_free_block(mnt, blk);
            inode_disk->direct[i] = 0;
        }
    }
    if (inode_disk->indirect) {
        struct buffer_head *bh = buffer_get(mnt->dev, inode_disk->indirect);
        if (bh) {
            uint32_t *entries = (uint32_t *)bh->data;
            size_t count = CAPYFS_BLOCK_SIZE / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (entries[i]) {
                    capyfs_free_block(mnt, entries[i]);
                    entries[i] = 0;
                }
            }
            buffer_mark_dirty(bh);
            buffer_release(bh);
        }
        capyfs_free_block(mnt, inode_disk->indirect);
        inode_disk->indirect = 0;
    }
    inode_disk->size = 0;
}

static int capyfs_get_data_block(struct capyfs_mount *mnt, struct capy_inode_disk *inode_disk, uint32_t logical_block, int allocate, uint32_t *out_block, int *inode_dirty) {
    if (logical_block < 12) {
        uint32_t block = inode_disk->direct[logical_block];
        if (block == 0 && allocate) {
            if (capyfs_alloc_block(mnt, &block) != 0) {
                return -1;
            }
            inode_disk->direct[logical_block] = block;
            if (inode_dirty) {
                *inode_dirty = 1;
            }
        }
        *out_block = block;
        return 0;
    }
    uint32_t indirect_index = logical_block - 12;
    uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / sizeof(uint32_t);
    if (indirect_index >= entries_per_block) {
        return -1;
    }
    uint32_t indirect_block = inode_disk->indirect;
    if (indirect_block == 0) {
        if (!allocate) {
            *out_block = 0;
            return 0;
        }
        if (capyfs_alloc_block(mnt, &indirect_block) != 0) {
            return -1;
        }
        inode_disk->indirect = indirect_block;
        if (inode_dirty) {
            *inode_dirty = 1;
        }
    }
    struct buffer_head *bh = buffer_get(mnt->dev, indirect_block);
    if (!bh) {
        return -1;
    }
    uint32_t *entries = (uint32_t *)bh->data;
    uint32_t block = entries[indirect_index];
    if (block == 0 && allocate) {
        if (capyfs_alloc_block(mnt, &block) != 0) {
            buffer_release(bh);
            return -1;
        }
        entries[indirect_index] = block;
        buffer_mark_dirty(bh);
    }
    buffer_release(bh);
    *out_block = block;
    return 0;
}

