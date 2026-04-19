#include "fs/capyfs.h"
#include "drivers/video/vga.h"

#include "fs/buffer.h"
#include "memory/kmem.h"

#define CAPYFS_DEBUG_CREATE 0

static inline void dbg_putc_serial(char ch) {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}

static void dbg_hex32_serial(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        dbg_putc_serial(hex[(value >> shift) & 0xFu]);
    }
}

static uint32_t dbg_be32_serial_load(const uint8_t *src) {
    if (!src) {
        return 0;
    }
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static void store_u32_le_volatile(volatile uint8_t *dst, uint32_t value) {
    if (!dst) {
        return;
    }
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void capyfs_dbg_puts(const char *msg) {
#if CAPYFS_DEBUG_CREATE
    if (msg) {
        vga_write(msg);
        vga_newline();
    }
#else
    (void)msg;
#endif
}

static void capyfs_dbg_hex32(const char *label, uint32_t value) {
#if CAPYFS_DEBUG_CREATE
    static const char hex[] = "0123456789ABCDEF";
    char buf[11]; buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        buf[2 + i] = hex[(value >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
    if (label) {
        vga_write(label);
    }
    vga_write(buf);
    vga_newline();
#else
    (void)label; (void)value;
#endif
}

struct capyfs_inode {
    struct capyfs_mount *mount;
    uint32_t ino;
};

struct capyfs_mount {
    struct block_device *dev;
    struct capy_super super;
};

static long capyfs_read(struct file *file, void *buffer, size_t size);
static long capyfs_write(struct file *file, const void *buffer, size_t size);
static int capyfs_open(struct inode *inode, struct file *file);
static int capyfs_close(struct file *file);
static int capyfs_lookup(struct inode *dir, const char *name, struct inode **out_inode);
static int capyfs_create(struct inode *dir, const char *name, uint16_t mode, const struct vfs_metadata *meta, struct inode **out_inode);
static int capyfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx);
static int capyfs_remove(struct inode *dir, const char *name, int is_dir);
static int capyfs_rename_inode(struct inode *src_dir, const char *src_name,
                               struct inode *dst_dir, const char *dst_name);
static int capyfs_stat_inode(struct inode *inode, struct vfs_stat *out);
static int capyfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta);

static struct file_ops capyfs_ops;
static int capyfs_ops_initialized = 0;

static void capyfs_init_ops(void) {
    if (capyfs_ops_initialized) {
        return;
    }
    capyfs_ops.open = capyfs_open;
    capyfs_ops.close = capyfs_close;
    capyfs_ops.lookup = capyfs_lookup;
    capyfs_ops.create = capyfs_create;
    capyfs_ops.read = capyfs_read;
    capyfs_ops.write = capyfs_write;
    capyfs_ops.iterate = capyfs_iterate;
    capyfs_ops.remove = capyfs_remove;
    capyfs_ops.rename = capyfs_rename_inode;
    capyfs_ops.stat = capyfs_stat_inode;
    capyfs_ops.set_metadata = capyfs_set_metadata;
    capyfs_ops_initialized = 1;
}

const struct file_ops *capyfs_file_ops(void) {
    capyfs_init_ops();
    return &capyfs_ops;
}

static struct capyfs_mount *inode_mount(struct inode *inode) {
    if (!inode || !inode->private_data) {
        return NULL;
    }
    struct capyfs_inode *ni = (struct capyfs_inode *)inode->private_data;
    return ni->mount;
}

static int names_equal(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static size_t cstring_length(const char *s) {
    size_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

static void cstring_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || !dst_size) {
        return;
    }
    size_t i = 0;
    if (src) {
        for (; i < dst_size - 1 && src[i]; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static void memory_zero(void *dst, size_t len) {
    uint8_t *p = (uint8_t *)dst;
    while (len--) {
        *p++ = 0;
    }
}

static int capyfs_format_finish(uint8_t *scratch, int scratch_heap, int rc) {
    if (scratch_heap && scratch) {
        kfree(scratch);
    }
    return rc;
}

static void zero_block(struct buffer_head *bh) {
    if (!bh) {
        return;
    }
    for (size_t i = 0; i < BUFFER_BLOCK_SIZE; ++i) {
        bh->data[i] = 0;
    }
    buffer_mark_dirty(bh);
}

static int capyfs_read_inode_disk(struct capyfs_mount *mnt, uint32_t ino, struct capy_inode_disk *out);
static int capyfs_write_inode_disk(struct capyfs_mount *mnt, uint32_t ino, const struct capy_inode_disk *src);
static int capyfs_alloc_inode(struct capyfs_mount *mnt, uint32_t *out_ino);
static int capyfs_alloc_block(struct capyfs_mount *mnt, uint32_t *out_block);
static int capyfs_dir_add_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, uint32_t dir_ino, uint32_t child_ino, const char *name);
static int capyfs_dir_find_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, const char *name, struct capy_dirent_disk *entry_out, uint32_t *block_index, uint32_t *entry_index);
static int capyfs_get_data_block(struct capyfs_mount *mnt, struct capy_inode_disk *inode_disk, uint32_t logical_block, int allocate, uint32_t *out_block, int *inode_dirty);
static struct inode *capyfs_create_vfs_inode(struct super_block *sb, struct capyfs_mount *mnt, uint32_t ino, const struct capy_inode_disk *disk);
static void capyfs_free_inode_bit(struct capyfs_mount *mnt, uint32_t ino);
static void capyfs_free_block(struct capyfs_mount *mnt, uint32_t block);
static void capyfs_free_data_blocks(struct capyfs_mount *mnt, struct capy_inode_disk *inode_disk);
static int capyfs_dir_clear_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, uint32_t dir_ino, uint32_t block_no, uint32_t entry_off);
static int capyfs_dir_is_empty(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode);

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
        inode_count = 64; // padrÃƒÆ’Ã‚Â£o simples
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
    return 0;
}

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

static int capyfs_lookup(struct inode *dir, const char *name, struct inode **out_inode) {
    capyfs_dbg_puts("[CAPYFS] lookup begin");
    if (dir) {
        capyfs_dbg_hex32("   dir ino=", dir->ino);
    }
    if (!dir || !out_inode || !name) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct capyfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }

    struct capy_inode_disk dir_disk;
    if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }
    dir->size = dir_disk.size;

    struct capy_dirent_disk found;
    if (capyfs_dir_find_entry(mnt, &dir_disk, name, &found, NULL, NULL) != 0) {
        return -1;
    }

    struct capy_inode_disk child_disk;
    if (capyfs_read_inode_disk(mnt, found.ino, &child_disk) != 0) {
        return -1;
    }

    struct inode *child = capyfs_create_vfs_inode(dir->sb, mnt, found.ino, &child_disk);
    if (!child) {
        return -1;
    }
    *out_inode = child;
    return 0;
}

static int capyfs_create(struct inode *dir, const char *name, uint16_t mode,
                         const struct vfs_metadata *meta, struct inode **out_inode) {
    capyfs_dbg_puts("[CAPYFS] create begin");
    capyfs_dbg_hex32("   dir ptr=", (uint32_t)(uintptr_t)dir);
    if (!dir || !name || !out_inode) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    size_t name_len = cstring_length(name);
    if (name_len == 0 || name_len >= CAPYFS_NAME_MAX) {
        return -1;
    }

    struct capyfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }

    struct capy_inode_disk dir_disk;
    if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        capyfs_dbg_puts("[CAPYFS] create: read dir inode failed");
        return -1;
    }
    capyfs_dbg_hex32("   dir size=", dir_disk.size);

    struct capy_dirent_disk existing;
    if (capyfs_dir_find_entry(mnt, &dir_disk, name, &existing, NULL, NULL) == 0) {
        capyfs_dbg_puts("[CAPYFS] create: entry already exists");
        return -1;
    }

    uint32_t new_ino;
    if (capyfs_alloc_inode(mnt, &new_ino) != 0) {
        capyfs_dbg_puts("[CAPYFS] create: alloc inode failed");
        return -1;
    }
    capyfs_dbg_hex32("   new ino=", new_ino);

    struct capy_inode_disk new_disk;
    new_disk.mode = mode;
    new_disk.links = 1;
    new_disk.size = 0;
    if (meta) {
        new_disk.uid = meta->uid;
        new_disk.gid = meta->gid;
        new_disk.perm = meta->perm;
    } else {
        new_disk.uid = 0;
        new_disk.gid = 0;
        new_disk.perm = (mode & VFS_MODE_DIR) ? 0755 : 0644;
    }
    new_disk.reserved = 0;
    for (size_t i = 0; i < 12; ++i) {
        new_disk.direct[i] = 0;
    }
    new_disk.indirect = 0;

    if (capyfs_write_inode_disk(mnt, new_ino, &new_disk) != 0) {
        capyfs_dbg_puts("[CAPYFS] create: write new inode failed");
        capyfs_free_inode_bit(mnt, new_ino);
        return -1;
    }

    if (capyfs_dir_add_entry(mnt, &dir_disk, dir->ino, new_ino, name) != 0) {
        capyfs_dbg_puts("[CAPYFS] create: add entry failed");
        capyfs_free_inode_bit(mnt, new_ino);
        return -1;
    }
    dir->size = dir_disk.size;

    struct inode *child = capyfs_create_vfs_inode(dir->sb, mnt, new_ino, &new_disk);
    if (!child) {
        capyfs_dbg_puts("[CAPYFS] create: vfs inode alloc failed");
        capyfs_free_inode_bit(mnt, new_ino);
        return -1;
    }
    capyfs_dbg_puts("[CAPYFS] create: success");
    *out_inode = child;
    return 0;
}

int capyfs_create_pub(struct inode *dir,
                      const char *name,
                      uint16_t mode,
                      const struct vfs_metadata *meta,
                      struct inode **out_inode) {
    return capyfs_create(dir, name, mode, meta, out_inode);
}

static int capyfs_remove(struct inode *dir, const char *name, int is_dir) {
    if (!dir || !name) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct capyfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }
    struct capy_inode_disk dir_disk;
    if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }

    struct capy_dirent_disk entry;
    uint32_t block_no = 0;
    uint32_t entry_off = 0;
    if (capyfs_dir_find_entry(mnt, &dir_disk, name, &entry, &block_no, &entry_off) != 0) {
        return -1;
    }

    struct capy_inode_disk target_disk;
    if (capyfs_read_inode_disk(mnt, entry.ino, &target_disk) != 0) {
        return -1;
    }
    int target_is_dir = (target_disk.mode & VFS_MODE_DIR) != 0;
    if (is_dir) {
        if (!target_is_dir) {
            return -1;
        }
        int empty = capyfs_dir_is_empty(mnt, &target_disk);
        if (empty != 1) {
            return -1;
        }
    } else if (target_is_dir) {
        return -1;
    }

    capyfs_free_data_blocks(mnt, &target_disk);
    if (capyfs_dir_clear_entry(mnt, &dir_disk, dir->ino, block_no, entry_off) != 0) {
        return -1;
    }

    struct capy_inode_disk zero_disk;
    memory_zero(&zero_disk, sizeof(zero_disk));
    if (capyfs_write_inode_disk(mnt, entry.ino, &zero_disk) != 0) {
        return -1;
    }
    capyfs_free_inode_bit(mnt, entry.ino);
    dir->size = dir_disk.size;
    return 0;
}

static int capyfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx) {
    if (!dir || !cb) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct capyfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }
    struct capy_inode_disk dir_disk;
    if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }
    dir->size = dir_disk.size;
    const uint32_t entry_size = sizeof(struct capy_dirent_disk);
    const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_disk.size / entry_size;

    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (capyfs_get_data_block(mnt, &dir_disk, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
        struct capy_dirent_disk entry = entries[entry_off];
        buffer_release(bh);
        if (entry.ino == 0 || entry.name[0] == '\0') {
            continue;
        }
        struct capy_inode_disk child_disk;
        if (capyfs_read_inode_disk(mnt, entry.ino, &child_disk) != 0) {
            return -1;
        }
        if (cb(entry.name, child_disk.mode, ctx) != 0) {
            break;
        }
    }
    return 0;
}

static int capyfs_rename_inode(struct inode *src_dir, const char *src_name,
                               struct inode *dst_dir, const char *dst_name) {
    if (!src_dir || !dst_dir || !src_name || !dst_name) {
        return -1;
    }
    if ((src_dir->mode & VFS_MODE_DIR) == 0 || (dst_dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct capyfs_mount *src_mnt = inode_mount(src_dir);
    struct capyfs_mount *dst_mnt = inode_mount(dst_dir);
    if (!src_mnt || !dst_mnt || src_mnt != dst_mnt) {
        return -1;
    }

    struct capy_inode_disk src_disk;
    struct capy_inode_disk dst_disk;
    if (capyfs_read_inode_disk(src_mnt, src_dir->ino, &src_disk) != 0) {
        return -1;
    }
    if (capyfs_read_inode_disk(dst_mnt, dst_dir->ino, &dst_disk) != 0) {
        return -1;
    }

    struct capy_dirent_disk entry;
    uint32_t src_block = 0;
    uint32_t src_off = 0;
    if (capyfs_dir_find_entry(src_mnt, &src_disk, src_name, &entry, &src_block, &src_off) != 0) {
        return -1;
    }

    if (src_dir->ino == dst_dir->ino) {
        if (names_equal(src_name, dst_name)) {
            return 0;
        }
        struct buffer_head *bh = buffer_get(src_mnt->dev, src_block);
        if (!bh) {
            return -1;
        }
        struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
        cstring_copy(entries[src_off].name, CAPYFS_NAME_MAX, dst_name);
        buffer_mark_dirty(bh);
        buffer_release(bh);
        return capyfs_write_inode_disk(src_mnt, src_dir->ino, &src_disk);
    }

    struct capy_dirent_disk existing;
    if (capyfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, NULL, NULL) == 0) {
        return -1;
    }

    if (capyfs_dir_add_entry(dst_mnt, &dst_disk, dst_dir->ino, entry.ino, dst_name) != 0) {
        return -1;
    }
    dst_dir->size = dst_disk.size;

    if (capyfs_dir_clear_entry(src_mnt, &src_disk, src_dir->ino, src_block, src_off) != 0) {
        // rollback destination entry
        uint32_t dst_block = 0;
        uint32_t dst_off = 0;
        if (capyfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, &dst_block, &dst_off) == 0) {
            capyfs_dir_clear_entry(dst_mnt, &dst_disk, dst_dir->ino, dst_block, dst_off);
        }
        return -1;
    }
    src_dir->size = src_disk.size;
    return 0;
}

static int capyfs_stat_inode(struct inode *inode, struct vfs_stat *out) {
    if (!inode || !out) {
        return -1;
    }
    out->ino = inode->ino;
    out->size = inode->size;
    out->uid = inode->uid;
    out->gid = inode->gid;
    out->mode = inode->mode;
    out->perm = inode->perm;
    return 0;
}

static int capyfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta) {
    if (!inode || !meta) {
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
    disk.uid = meta->uid;
    disk.gid = meta->gid;
    disk.perm = meta->perm;
    if (capyfs_write_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }
    inode->uid = disk.uid;
    inode->gid = disk.gid;
    inode->perm = disk.perm;
    return 0;
}

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

static int capyfs_dir_add_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, uint32_t dir_ino, uint32_t child_ino, const char *name) {
    if (!mnt || !dir_inode || !name) {
        return -1;
    }
    capyfs_dbg_puts("[CAPYFS] dir_add begin");
    capyfs_dbg_hex32("   dir_ino=", dir_ino);
    capyfs_dbg_hex32("   child_ino=", child_ino);
    size_t name_len = cstring_length(name);
    if (name_len == 0 || name_len >= CAPYFS_NAME_MAX) {
        return -1;
    }

    const uint32_t entry_size = sizeof(struct capy_dirent_disk);
    const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;

    if (capyfs_read_inode_disk(mnt, dir_ino, dir_inode) != 0) {
        capyfs_dbg_puts("[CAPYFS] dir_add: failed to refresh dir_inode");
        return -1;
    }

    uint32_t entry_count = dir_inode->size / entry_size;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (capyfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            capyfs_dbg_puts("[CAPYFS] dir_add: get_data_block existing failed");
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            capyfs_dbg_puts("[CAPYFS] dir_add: buffer_get existing failed");
            return -1;
        }
        struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
        if (entries[entry_off].ino == 0) {
            entries[entry_off].ino = child_ino;
            cstring_copy(entries[entry_off].name, CAPYFS_NAME_MAX, name);
            buffer_mark_dirty(bh);
            buffer_release(bh);
            capyfs_write_inode_disk(mnt, dir_ino, dir_inode);
            return 0;
        }
        buffer_release(bh);
    }

    uint32_t new_index = entry_count;
    uint32_t logical_block = new_index / entries_per_block;
    uint32_t entry_off = new_index % entries_per_block;
    uint32_t block_no;
    int inode_dirty = 0;
    if (capyfs_get_data_block(mnt, dir_inode, logical_block, 1, &block_no, &inode_dirty) != 0) {
        capyfs_dbg_puts("[CAPYFS] dir_add: get_data_block alloc failed");
        return -1;
    }
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        capyfs_dbg_puts("[CAPYFS] dir_add: buffer_get alloc failed");
        return -1;
    }
    struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
    entries[entry_off].ino = child_ino;
    cstring_copy(entries[entry_off].name, CAPYFS_NAME_MAX, name);
    buffer_mark_dirty(bh);
    buffer_release(bh);

    dir_inode->size = (new_index + 1) * entry_size;
    capyfs_write_inode_disk(mnt, dir_ino, dir_inode);
    capyfs_dbg_puts("[CAPYFS] dir_add end");
    return 0;
}

static int capyfs_dir_find_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, const char *name, struct capy_dirent_disk *entry_out, uint32_t *block_index, uint32_t *entry_index) {
    if (!mnt || !dir_inode || !name) {
        return -1;
    }
    const uint32_t entry_size = sizeof(struct capy_dirent_disk);
    const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_inode->size / entry_size;

    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (capyfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
        struct capy_dirent_disk *entry = &entries[entry_off];
        if (entry->ino != 0 && names_equal(entry->name, name)) {
            if (entry_out) {
                *entry_out = *entry;
            }
            if (block_index) {
                *block_index = block_no;
            }
            if (entry_index) {
                *entry_index = entry_off;
            }
            buffer_release(bh);
            return 0;
        }
        buffer_release(bh);
    }
    return -1;
}

static int capyfs_dir_clear_entry(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode, uint32_t dir_ino, uint32_t block_no, uint32_t entry_off) {
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
    entries[entry_off].ino = 0;
    entries[entry_off].name[0] = '\0';
    buffer_mark_dirty(bh);
    buffer_release(bh);
    return capyfs_write_inode_disk(mnt, dir_ino, dir_inode);
}

static int capyfs_dir_is_empty(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode) {
    const uint32_t entry_size = sizeof(struct capy_dirent_disk);
    const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_inode->size / entry_size;
    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (capyfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
        struct capy_dirent_disk entry = entries[entry_off];
        buffer_release(bh);
        if (entry.ino != 0) {
            return 0;
        }
    }
    return 1;
}
