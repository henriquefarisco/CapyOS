static int capyfs_open(struct inode *inode, struct file *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int capyfs_close(struct file *file) {
    (void)file;
    return 0;
}

static long capyfs_read(struct file *file, void *buffer, size_t size) {
    if (!file || !buffer || size == 0) {
        return 0;
    }
    if (!file->dentry || !file->dentry->inode) {
        return -1;
    }
    struct inode *inode = file->dentry->inode;
    if ((inode->mode & VFS_MODE_DIR) != 0) {
        return -1;
    }
    struct capyfs_mount *mnt = inode_mount(inode);
    if (!mnt) {
        return -1;
    }

    struct capy_inode_disk disk;
    if (capyfs_read_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }
    inode->size = disk.size;

    uint32_t pos = file->position;
    if (pos >= disk.size) {
        return 0;
    }

    size_t remaining = size;
    if (remaining > disk.size - pos) {
        remaining = disk.size - pos;
    }

    uint8_t *dst = (uint8_t *)buffer;
    size_t copied = 0;
    while (copied < remaining) {
        uint32_t absolute = pos + (uint32_t)copied;
        uint32_t block_index = absolute / CAPYFS_BLOCK_SIZE;
        uint32_t block_offset = absolute % CAPYFS_BLOCK_SIZE;
        size_t chunk = CAPYFS_BLOCK_SIZE - block_offset;
        size_t left = remaining - copied;
        if (chunk > left) {
            chunk = left;
        }

        uint32_t block_no;
        int inode_dirty = 0;
        if (capyfs_get_data_block(mnt, &disk, block_index, 0, &block_no, &inode_dirty) != 0) {
            break;
        }

        if (block_no == 0) {
            for (size_t i = 0; i < chunk; ++i) {
                dst[copied + i] = 0;
            }
        } else {
            struct buffer_head *bh = buffer_get(mnt->dev, block_no);
            if (!bh) {
                break;
            }
            for (size_t i = 0; i < chunk; ++i) {
                dst[copied + i] = bh->data[block_offset + i];
            }
            buffer_release(bh);
        }
        copied += chunk;
    }
    return (long)copied;
}

static long capyfs_write(struct file *file, const void *buffer, size_t size) {
    if (!file || (!buffer && size > 0)) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }
    if (!file->dentry || !file->dentry->inode) {
        return -1;
    }
    struct inode *inode = file->dentry->inode;
    if ((inode->mode & VFS_MODE_DIR) != 0) {
        return -1;
    }
    struct capyfs_mount *mnt = inode_mount(inode);
    if (!mnt) {
        return -1;
    }

    struct capy_inode_disk disk;
    if (capyfs_read_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }

    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t pos = file->position;
    size_t written = 0;
    int inode_dirty = 0;
    uint32_t new_size = disk.size;

    while (written < size) {
        uint32_t absolute = pos + (uint32_t)written;
        uint32_t block_index = absolute / CAPYFS_BLOCK_SIZE;
        uint32_t block_offset = absolute % CAPYFS_BLOCK_SIZE;
        size_t chunk = CAPYFS_BLOCK_SIZE - block_offset;
        size_t left = size - written;
        if (chunk > left) {
            chunk = left;
        }

        uint32_t block_no;
        if (capyfs_get_data_block(mnt, &disk, block_index, 1, &block_no, &inode_dirty) != 0 || block_no == 0) {
            break;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            break;
        }
        for (size_t i = 0; i < chunk; ++i) {
            bh->data[block_offset + i] = src[written + i];
        }
        buffer_mark_dirty(bh);
        buffer_release(bh);
        written += chunk;
    }

    uint32_t final_size = pos + (uint32_t)written;
    if (final_size > new_size) {
        new_size = final_size;
    }
    if (new_size != disk.size) {
        disk.size = new_size;
        inode_dirty = 1;
    }
    if (inode_dirty) {
        if (capyfs_write_inode_disk(mnt, inode->ino, &disk) != 0) {
            return (long)written;
        }
    }
    inode->size = disk.size;
    return (long)written;
}

