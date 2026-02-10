#include "fs/noirfs.h"
#include "drivers/video/vga.h"

#include "fs/buffer.h"
#include "memory/kmem.h"

#define NOIRFS_DEBUG_CREATE 0

static void noirfs_dbg_puts(const char *msg) {
#if NOIRFS_DEBUG_CREATE
    if (msg) {
        vga_write(msg);
        vga_newline();
    }
#else
    (void)msg;
#endif
}

static void noirfs_dbg_hex32(const char *label, uint32_t value) {
#if NOIRFS_DEBUG_CREATE
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

struct noirfs_inode {
    struct noirfs_mount *mount;
    uint32_t ino;
};

struct noirfs_mount {
    struct block_device *dev;
    struct noir_super super;
};

static long noirfs_read(struct file *file, void *buffer, size_t size);
static long noirfs_write(struct file *file, const void *buffer, size_t size);
static int noirfs_open(struct inode *inode, struct file *file);
static int noirfs_close(struct file *file);
static int noirfs_lookup(struct inode *dir, const char *name, struct inode **out_inode);
static int noirfs_create(struct inode *dir, const char *name, uint16_t mode, const struct vfs_metadata *meta, struct inode **out_inode);
static int noirfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx);
static int noirfs_remove(struct inode *dir, const char *name, int is_dir);
static int noirfs_rename_inode(struct inode *src_dir, const char *src_name,
                               struct inode *dst_dir, const char *dst_name);
static int noirfs_stat_inode(struct inode *inode, struct vfs_stat *out);
static int noirfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta);

static struct file_ops noirfs_ops;
static int noirfs_ops_initialized = 0;

static void noirfs_init_ops(void) {
    if (noirfs_ops_initialized) {
        return;
    }
    noirfs_ops.open = noirfs_open;
    noirfs_ops.close = noirfs_close;
    noirfs_ops.lookup = noirfs_lookup;
    noirfs_ops.create = noirfs_create;
    noirfs_ops.read = noirfs_read;
    noirfs_ops.write = noirfs_write;
    noirfs_ops.iterate = noirfs_iterate;
    noirfs_ops.remove = noirfs_remove;
    noirfs_ops.rename = noirfs_rename_inode;
    noirfs_ops.stat = noirfs_stat_inode;
    noirfs_ops.set_metadata = noirfs_set_metadata;
    noirfs_ops_initialized = 1;
}

const struct file_ops *noirfs_file_ops(void) {
    noirfs_init_ops();
    return &noirfs_ops;
}

static struct noirfs_mount *inode_mount(struct inode *inode) {
    if (!inode || !inode->private_data) {
        return NULL;
    }
    struct noirfs_inode *ni = (struct noirfs_inode *)inode->private_data;
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

static void zero_block(struct buffer_head *bh) {
    if (!bh) {
        return;
    }
    for (size_t i = 0; i < BUFFER_BLOCK_SIZE; ++i) {
        bh->data[i] = 0;
    }
    buffer_mark_dirty(bh);
}

static int noirfs_read_inode_disk(struct noirfs_mount *mnt, uint32_t ino, struct noir_inode_disk *out);
static int noirfs_write_inode_disk(struct noirfs_mount *mnt, uint32_t ino, const struct noir_inode_disk *src);
static int noirfs_alloc_inode(struct noirfs_mount *mnt, uint32_t *out_ino);
static int noirfs_alloc_block(struct noirfs_mount *mnt, uint32_t *out_block);
static int noirfs_dir_add_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, uint32_t dir_ino, uint32_t child_ino, const char *name);
static int noirfs_dir_find_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, const char *name, struct noir_dirent_disk *entry_out, uint32_t *block_index, uint32_t *entry_index);
static int noirfs_get_data_block(struct noirfs_mount *mnt, struct noir_inode_disk *inode_disk, uint32_t logical_block, int allocate, uint32_t *out_block, int *inode_dirty);
static struct inode *noirfs_create_vfs_inode(struct super_block *sb, struct noirfs_mount *mnt, uint32_t ino, const struct noir_inode_disk *disk);
static void noirfs_free_inode_bit(struct noirfs_mount *mnt, uint32_t ino);
static void noirfs_free_block(struct noirfs_mount *mnt, uint32_t block);
static void noirfs_free_data_blocks(struct noirfs_mount *mnt, struct noir_inode_disk *inode_disk);
static int noirfs_dir_clear_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, uint32_t dir_ino, uint32_t block_no, uint32_t entry_off);
static int noirfs_dir_is_empty(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode);

int noirfs_format(struct block_device *dev,
                  uint32_t inode_count,
                  uint32_t block_count,
                  noirfs_progress_cb progress) {
    if (!dev || dev->block_size != NOIRFS_BLOCK_SIZE) {
        return -10;
    }
    if (block_count == 0 || block_count > dev->block_count) {
        block_count = dev->block_count;
    }

    uint32_t bits_per_block = NOIRFS_BLOCK_SIZE * 8;
    if (inode_count == 0) {
        inode_count = 64; // padrão simples
    }
    uint32_t block_bitmap_blocks = (block_count + bits_per_block - 1) / bits_per_block;
    uint32_t inode_bitmap_blocks = (inode_count + bits_per_block - 1) / bits_per_block;
    uint32_t inode_table_blocks = (inode_count * sizeof(struct noir_inode_disk) + NOIRFS_BLOCK_SIZE - 1) / NOIRFS_BLOCK_SIZE;

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

    struct noir_super super;
    super.magic = NOIRFS_MAGIC;
    super.version = NOIRFS_VERSION;
    super.block_size = NOIRFS_BLOCK_SIZE;
    super.block_count = block_count;
    super.inode_count = inode_count;
    super.bmap_start = bmap_start;
    super.imap_start = imap_start;
    super.inode_start = inode_start;
    super.data_start = data_start;

    // Escreve superbloco via buffer cache (um bloco completo)
    struct buffer_head *sbh = buffer_get(dev, 0);
    if (!sbh) {
        return -12;
    }
    zero_block(sbh);
    for (size_t i = 0; i < sizeof(struct noir_super); ++i) {
        sbh->data[i] = ((const uint8_t *)&super)[i];
    }
    buffer_mark_dirty(sbh);
    buffer_release(sbh);

    if (progress) {
        progress("Superbloco", 5);
    }

    struct buffer_head *bh;
    uint32_t meta_blocks = (data_start > bmap_start) ? (data_start - bmap_start) : 0;
    noirfs_dbg_puts("[noirfs] limpando metadados begin");
    for (uint32_t i = bmap_start; i < data_start; ++i) {
        bh = buffer_get(dev, i);
        if (!bh) {
            return -13;
        }
        zero_block(bh);
        buffer_release(bh);

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
    noirfs_dbg_puts("[noirfs] reservando blocos begin");
    if (progress) {
        progress("Reservando blocos", 72);
    }
    for (uint32_t blk = 0; blk < used_blocks; ++blk) {
        uint32_t index = blk;
        uint32_t rel = index % (NOIRFS_BLOCK_SIZE * 8);
        uint32_t block_idx = super.bmap_start + index / (NOIRFS_BLOCK_SIZE * 8);
        struct buffer_head *map_bh = buffer_get(dev, block_idx);
        if (!map_bh) {
            return -14;
        }
        uint32_t byte = rel / 8;
        uint32_t bit = rel % 8;
        map_bh->data[byte] |= (1u << bit);
        buffer_mark_dirty(map_bh);
        buffer_release(map_bh);

        if (progress && used_blocks) {
            uint32_t pct = 70 + ((blk + 1) * 15) / used_blocks;
            if (pct > 85) pct = 85;
            progress("Reservando blocos", pct);
        }
    }

    if (progress) {
        progress("Reservando blocos", 85);
    }

    uint32_t root_ino = 0;
    uint32_t rel = root_ino % (NOIRFS_BLOCK_SIZE * 8);
    uint32_t map_block = super.imap_start + root_ino / (NOIRFS_BLOCK_SIZE * 8);
    struct buffer_head *imap_bh = buffer_get(dev, map_block);
    if (!imap_bh) {
        return -15;
    }
    uint32_t byte = rel / 8;
    uint32_t bit = rel % 8;
    imap_bh->data[byte] |= (1u << bit);
    buffer_mark_dirty(imap_bh);
    buffer_release(imap_bh);

    struct noir_inode_disk root_disk;
    root_disk.mode = VFS_MODE_DIR;
    root_disk.links = 1;
    root_disk.size = 0;
    root_disk.uid = 0;
    root_disk.gid = 0;
    root_disk.perm = 0755;
    root_disk.reserved = 0;
    for (size_t i = 0; i < 12; ++i) {
        root_disk.direct[i] = 0;
    }
    root_disk.indirect = 0;
    struct noirfs_mount fake_mount = { .dev = dev, .super = super };
    if (noirfs_write_inode_disk(&fake_mount, root_ino, &root_disk) != 0) {
        return -16;
    }

    if (progress) {
        progress("Criando raiz", 95);
    }
    buffer_cache_sync(dev);
    if (progress) {
        progress("Concluido", 100);
    }
    return 0;
}

int mount_noirfs(struct block_device *dev, struct super_block *sb) {
    if (!dev || !sb) {
        return -1;
    }
    if (dev->block_size != NOIRFS_BLOCK_SIZE) {
        return -1;
    }

    struct buffer_head *bh = buffer_get(dev, 0);
    if (!bh) {
        return -1;
    }

    struct noir_super *disk_super = (struct noir_super *)bh->data;
    if (disk_super->magic != NOIRFS_MAGIC || disk_super->block_size != NOIRFS_BLOCK_SIZE) {
        buffer_release(bh);
        return -1;
    }

    struct noirfs_mount *mnt = (struct noirfs_mount *)kalloc(sizeof(struct noirfs_mount));
    if (!mnt) {
        buffer_release(bh);
        return -1;
    }
    mnt->dev = dev;
    mnt->super = *disk_super;
    buffer_release(bh);

    sb->bdev = dev;
    sb->fs_private = mnt;

    struct noir_inode_disk root_disk;
    if (noirfs_read_inode_disk(mnt, 0, &root_disk) != 0) {
        kfree(mnt);
        return -1;
    }

    struct inode *root_inode = noirfs_create_vfs_inode(sb, mnt, 0, &root_disk);
    if (!root_inode) {
        kfree(mnt);
        return -1;
    }

    struct dentry *root_dentry = (struct dentry *)kalloc(sizeof(struct dentry));
    if (!root_dentry) {
        kfree(root_inode->private_data);
        kfree(root_inode);
        kfree(mnt);
        return -1;
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

static int noirfs_open(struct inode *inode, struct file *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int noirfs_close(struct file *file) {
    (void)file;
    return 0;
}

static long noirfs_read(struct file *file, void *buffer, size_t size) {
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
    struct noirfs_mount *mnt = inode_mount(inode);
    if (!mnt) {
        return -1;
    }

    struct noir_inode_disk disk;
    if (noirfs_read_inode_disk(mnt, inode->ino, &disk) != 0) {
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
        uint32_t block_index = absolute / NOIRFS_BLOCK_SIZE;
        uint32_t block_offset = absolute % NOIRFS_BLOCK_SIZE;
        size_t chunk = NOIRFS_BLOCK_SIZE - block_offset;
        size_t left = remaining - copied;
        if (chunk > left) {
            chunk = left;
        }

        uint32_t block_no;
        int inode_dirty = 0;
        if (noirfs_get_data_block(mnt, &disk, block_index, 0, &block_no, &inode_dirty) != 0) {
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

static long noirfs_write(struct file *file, const void *buffer, size_t size) {
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
    struct noirfs_mount *mnt = inode_mount(inode);
    if (!mnt) {
        return -1;
    }

    struct noir_inode_disk disk;
    if (noirfs_read_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }

    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t pos = file->position;
    size_t written = 0;
    int inode_dirty = 0;
    uint32_t new_size = disk.size;

    while (written < size) {
        uint32_t absolute = pos + (uint32_t)written;
        uint32_t block_index = absolute / NOIRFS_BLOCK_SIZE;
        uint32_t block_offset = absolute % NOIRFS_BLOCK_SIZE;
        size_t chunk = NOIRFS_BLOCK_SIZE - block_offset;
        size_t left = size - written;
        if (chunk > left) {
            chunk = left;
        }

        uint32_t block_no;
        if (noirfs_get_data_block(mnt, &disk, block_index, 1, &block_no, &inode_dirty) != 0 || block_no == 0) {
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
        if (noirfs_write_inode_disk(mnt, inode->ino, &disk) != 0) {
            return (long)written;
        }
    }
    inode->size = disk.size;
    return (long)written;
}

static int noirfs_lookup(struct inode *dir, const char *name, struct inode **out_inode) {
    noirfs_dbg_puts("[noirfs] lookup begin");
    if (dir) {
        noirfs_dbg_hex32("   dir ino=", dir->ino);
    }
    if (name) { vga_write("   name: "); vga_write(name); vga_newline(); }
    if (!dir || !out_inode || !name) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct noirfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }

    struct noir_inode_disk dir_disk;
    if (noirfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }
    dir->size = dir_disk.size;

    struct noir_dirent_disk found;
    if (noirfs_dir_find_entry(mnt, &dir_disk, name, &found, NULL, NULL) != 0) {
        return -1;
    }

    struct noir_inode_disk child_disk;
    if (noirfs_read_inode_disk(mnt, found.ino, &child_disk) != 0) {
        return -1;
    }

    struct inode *child = noirfs_create_vfs_inode(dir->sb, mnt, found.ino, &child_disk);
    if (!child) {
        return -1;
    }
    *out_inode = child;
    return 0;
}

static int noirfs_create(struct inode *dir, const char *name, uint16_t mode,
                         const struct vfs_metadata *meta, struct inode **out_inode) {
    noirfs_dbg_puts("[noirfs] create begin");
    if (name) { vga_write("   target: "); vga_write(name); vga_newline(); }
    noirfs_dbg_hex32("   dir ptr=", (uint32_t)(uintptr_t)dir);
    if (!dir || !name || !out_inode) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    size_t name_len = cstring_length(name);
    if (name_len == 0 || name_len >= NOIRFS_NAME_MAX) {
        return -1;
    }

    struct noirfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }

    struct noir_inode_disk dir_disk;
    if (noirfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        noirfs_dbg_puts("[noirfs] create: read dir inode failed");
        return -1;
    }
    noirfs_dbg_hex32("   dir size=", dir_disk.size);

    struct noir_dirent_disk existing;
    if (noirfs_dir_find_entry(mnt, &dir_disk, name, &existing, NULL, NULL) == 0) {
        noirfs_dbg_puts("[noirfs] create: entry already exists");
        return -1;
    }

    uint32_t new_ino;
    if (noirfs_alloc_inode(mnt, &new_ino) != 0) {
        noirfs_dbg_puts("[noirfs] create: alloc inode failed");
        return -1;
    }
    noirfs_dbg_hex32("   new ino=", new_ino);

    struct noir_inode_disk new_disk;
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

    if (noirfs_write_inode_disk(mnt, new_ino, &new_disk) != 0) {
        noirfs_dbg_puts("[noirfs] create: write new inode failed");
        noirfs_free_inode_bit(mnt, new_ino);
        return -1;
    }

    if (noirfs_dir_add_entry(mnt, &dir_disk, dir->ino, new_ino, name) != 0) {
        noirfs_dbg_puts("[noirfs] create: add entry failed");
        noirfs_free_inode_bit(mnt, new_ino);
        return -1;
    }
    dir->size = dir_disk.size;

    struct inode *child = noirfs_create_vfs_inode(dir->sb, mnt, new_ino, &new_disk);
    if (!child) {
        noirfs_dbg_puts("[noirfs] create: vfs inode alloc failed");
        noirfs_free_inode_bit(mnt, new_ino);
        return -1;
    }
    noirfs_dbg_puts("[noirfs] create: success");
    *out_inode = child;
    return 0;
}

int noirfs_create_pub(struct inode *dir,
                      const char *name,
                      uint16_t mode,
                      const struct vfs_metadata *meta,
                      struct inode **out_inode) {
    return noirfs_create(dir, name, mode, meta, out_inode);
}

static int noirfs_remove(struct inode *dir, const char *name, int is_dir) {
    if (!dir || !name) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct noirfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }
    struct noir_inode_disk dir_disk;
    if (noirfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }

    struct noir_dirent_disk entry;
    uint32_t block_no = 0;
    uint32_t entry_off = 0;
    if (noirfs_dir_find_entry(mnt, &dir_disk, name, &entry, &block_no, &entry_off) != 0) {
        return -1;
    }

    struct noir_inode_disk target_disk;
    if (noirfs_read_inode_disk(mnt, entry.ino, &target_disk) != 0) {
        return -1;
    }
    int target_is_dir = (target_disk.mode & VFS_MODE_DIR) != 0;
    if (is_dir) {
        if (!target_is_dir) {
            return -1;
        }
        int empty = noirfs_dir_is_empty(mnt, &target_disk);
        if (empty != 1) {
            return -1;
        }
    } else if (target_is_dir) {
        return -1;
    }

    noirfs_free_data_blocks(mnt, &target_disk);
    if (noirfs_dir_clear_entry(mnt, &dir_disk, dir->ino, block_no, entry_off) != 0) {
        return -1;
    }

    struct noir_inode_disk zero_disk;
    memory_zero(&zero_disk, sizeof(zero_disk));
    if (noirfs_write_inode_disk(mnt, entry.ino, &zero_disk) != 0) {
        return -1;
    }
    noirfs_free_inode_bit(mnt, entry.ino);
    dir->size = dir_disk.size;
    return 0;
}

static int noirfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx) {
    if (!dir || !cb) {
        return -1;
    }
    if ((dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct noirfs_mount *mnt = inode_mount(dir);
    if (!mnt) {
        return -1;
    }
    struct noir_inode_disk dir_disk;
    if (noirfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) {
        return -1;
    }
    dir->size = dir_disk.size;
    const uint32_t entry_size = sizeof(struct noir_dirent_disk);
    const uint32_t entries_per_block = NOIRFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_disk.size / entry_size;

    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (noirfs_get_data_block(mnt, &dir_disk, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
        struct noir_dirent_disk entry = entries[entry_off];
        buffer_release(bh);
        if (entry.ino == 0 || entry.name[0] == '\0') {
            continue;
        }
        struct noir_inode_disk child_disk;
        if (noirfs_read_inode_disk(mnt, entry.ino, &child_disk) != 0) {
            return -1;
        }
        if (cb(entry.name, child_disk.mode, ctx) != 0) {
            break;
        }
    }
    return 0;
}

static int noirfs_rename_inode(struct inode *src_dir, const char *src_name,
                               struct inode *dst_dir, const char *dst_name) {
    if (!src_dir || !dst_dir || !src_name || !dst_name) {
        return -1;
    }
    if ((src_dir->mode & VFS_MODE_DIR) == 0 || (dst_dir->mode & VFS_MODE_DIR) == 0) {
        return -1;
    }
    struct noirfs_mount *src_mnt = inode_mount(src_dir);
    struct noirfs_mount *dst_mnt = inode_mount(dst_dir);
    if (!src_mnt || !dst_mnt || src_mnt != dst_mnt) {
        return -1;
    }

    struct noir_inode_disk src_disk;
    struct noir_inode_disk dst_disk;
    if (noirfs_read_inode_disk(src_mnt, src_dir->ino, &src_disk) != 0) {
        return -1;
    }
    if (noirfs_read_inode_disk(dst_mnt, dst_dir->ino, &dst_disk) != 0) {
        return -1;
    }

    struct noir_dirent_disk entry;
    uint32_t src_block = 0;
    uint32_t src_off = 0;
    if (noirfs_dir_find_entry(src_mnt, &src_disk, src_name, &entry, &src_block, &src_off) != 0) {
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
        struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
        cstring_copy(entries[src_off].name, NOIRFS_NAME_MAX, dst_name);
        buffer_mark_dirty(bh);
        buffer_release(bh);
        return noirfs_write_inode_disk(src_mnt, src_dir->ino, &src_disk);
    }

    struct noir_dirent_disk existing;
    if (noirfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, NULL, NULL) == 0) {
        return -1;
    }

    if (noirfs_dir_add_entry(dst_mnt, &dst_disk, dst_dir->ino, entry.ino, dst_name) != 0) {
        return -1;
    }
    dst_dir->size = dst_disk.size;

    if (noirfs_dir_clear_entry(src_mnt, &src_disk, src_dir->ino, src_block, src_off) != 0) {
        // rollback destination entry
        uint32_t dst_block = 0;
        uint32_t dst_off = 0;
        if (noirfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, &dst_block, &dst_off) == 0) {
            noirfs_dir_clear_entry(dst_mnt, &dst_disk, dst_dir->ino, dst_block, dst_off);
        }
        return -1;
    }
    src_dir->size = src_disk.size;
    return 0;
}

static int noirfs_stat_inode(struct inode *inode, struct vfs_stat *out) {
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

static int noirfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta) {
    if (!inode || !meta) {
        return -1;
    }
    struct noirfs_mount *mnt = inode_mount(inode);
    if (!mnt) {
        return -1;
    }
    struct noir_inode_disk disk;
    if (noirfs_read_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }
    disk.uid = meta->uid;
    disk.gid = meta->gid;
    disk.perm = meta->perm;
    if (noirfs_write_inode_disk(mnt, inode->ino, &disk) != 0) {
        return -1;
    }
    inode->uid = disk.uid;
    inode->gid = disk.gid;
    inode->perm = disk.perm;
    return 0;
}

static int noirfs_read_inode_disk(struct noirfs_mount *mnt, uint32_t ino, struct noir_inode_disk *out) {
    if (!mnt || !out || ino >= mnt->super.inode_count) {
        return -1;
    }
    uint32_t per_block = NOIRFS_BLOCK_SIZE / sizeof(struct noir_inode_disk);
    uint32_t block_off = ino / per_block;
    uint32_t index = ino % per_block;
    uint32_t block_no = mnt->super.inode_start + block_off;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct noir_inode_disk *base = (struct noir_inode_disk *)bh->data;
    *out = base[index];
    buffer_release(bh);
    return 0;
}

static int noirfs_write_inode_disk(struct noirfs_mount *mnt, uint32_t ino, const struct noir_inode_disk *src) {
    if (!mnt || !src || ino >= mnt->super.inode_count) {
        return -1;
    }
    uint32_t per_block = NOIRFS_BLOCK_SIZE / sizeof(struct noir_inode_disk);
    uint32_t block_off = ino / per_block;
    uint32_t index = ino % per_block;
    uint32_t block_no = mnt->super.inode_start + block_off;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct noir_inode_disk *base = (struct noir_inode_disk *)bh->data;
    base[index] = *src;
    buffer_mark_dirty(bh);
    buffer_release(bh);
    buffer_cache_sync(mnt->dev);
    return 0;
}

static struct inode *noirfs_create_vfs_inode(struct super_block *sb, struct noirfs_mount *mnt, uint32_t ino, const struct noir_inode_disk *disk) {
    struct inode *inode = (struct inode *)kalloc(sizeof(struct inode));
    if (!inode) {
        return NULL;
    }
    struct noirfs_inode *priv = (struct noirfs_inode *)kalloc(sizeof(struct noirfs_inode));
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
    inode->ops = noirfs_file_ops();
    inode->private_data = priv;
    return inode;
}

static int noirfs_alloc_inode(struct noirfs_mount *mnt, uint32_t *out_ino) {
    uint32_t bits_per_block = NOIRFS_BLOCK_SIZE * 8;
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


static void noirfs_free_inode_bit(struct noirfs_mount *mnt, uint32_t ino) {
    uint32_t bits_per_block = NOIRFS_BLOCK_SIZE * 8;
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

static void noirfs_free_block(struct noirfs_mount *mnt, uint32_t block) {
    uint32_t bits_per_block = NOIRFS_BLOCK_SIZE * 8;
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

static int noirfs_alloc_block(struct noirfs_mount *mnt, uint32_t *out_block) {
    uint32_t bits_per_block = NOIRFS_BLOCK_SIZE * 8;
    for (uint32_t blk = mnt->super.data_start; blk < mnt->super.block_count; ++blk) {
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
            return 0;
        }
        buffer_release(bh);
    }
    return -1;
}

static void noirfs_free_data_blocks(struct noirfs_mount *mnt, struct noir_inode_disk *inode_disk) {
    if (!mnt || !inode_disk) {
        return;
    }
    for (size_t i = 0; i < 12; ++i) {
        uint32_t blk = inode_disk->direct[i];
        if (blk) {
            noirfs_free_block(mnt, blk);
            inode_disk->direct[i] = 0;
        }
    }
    if (inode_disk->indirect) {
        struct buffer_head *bh = buffer_get(mnt->dev, inode_disk->indirect);
        if (bh) {
            uint32_t *entries = (uint32_t *)bh->data;
            size_t count = NOIRFS_BLOCK_SIZE / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (entries[i]) {
                    noirfs_free_block(mnt, entries[i]);
                    entries[i] = 0;
                }
            }
            buffer_mark_dirty(bh);
            buffer_release(bh);
        }
        noirfs_free_block(mnt, inode_disk->indirect);
        inode_disk->indirect = 0;
    }
    inode_disk->size = 0;
}

static int noirfs_get_data_block(struct noirfs_mount *mnt, struct noir_inode_disk *inode_disk, uint32_t logical_block, int allocate, uint32_t *out_block, int *inode_dirty) {
    if (logical_block < 12) {
        uint32_t block = inode_disk->direct[logical_block];
        if (block == 0 && allocate) {
            if (noirfs_alloc_block(mnt, &block) != 0) {
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
    uint32_t entries_per_block = NOIRFS_BLOCK_SIZE / sizeof(uint32_t);
    if (indirect_index >= entries_per_block) {
        return -1;
    }
    uint32_t indirect_block = inode_disk->indirect;
    if (indirect_block == 0) {
        if (!allocate) {
            *out_block = 0;
            return 0;
        }
        if (noirfs_alloc_block(mnt, &indirect_block) != 0) {
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
        if (noirfs_alloc_block(mnt, &block) != 0) {
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

static int noirfs_dir_add_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, uint32_t dir_ino, uint32_t child_ino, const char *name) {
    if (!mnt || !dir_inode || !name) {
        return -1;
    }
    noirfs_dbg_puts("[noirfs] dir_add begin");
    noirfs_dbg_hex32("   dir_ino=", dir_ino);
    noirfs_dbg_hex32("   child_ino=", child_ino);
    size_t name_len = cstring_length(name);
    if (name_len == 0 || name_len >= NOIRFS_NAME_MAX) {
        return -1;
    }

    const uint32_t entry_size = sizeof(struct noir_dirent_disk);
    const uint32_t entries_per_block = NOIRFS_BLOCK_SIZE / entry_size;

    if (noirfs_read_inode_disk(mnt, dir_ino, dir_inode) != 0) {
        noirfs_dbg_puts("[noirfs] dir_add: failed to refresh dir_inode");
        return -1;
    }

    uint32_t entry_count = dir_inode->size / entry_size;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (noirfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            noirfs_dbg_puts("[noirfs] dir_add: get_data_block existing failed");
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            noirfs_dbg_puts("[noirfs] dir_add: buffer_get existing failed");
            return -1;
        }
        struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
        if (entries[entry_off].ino == 0) {
            entries[entry_off].ino = child_ino;
            cstring_copy(entries[entry_off].name, NOIRFS_NAME_MAX, name);
            buffer_mark_dirty(bh);
            buffer_release(bh);
            noirfs_write_inode_disk(mnt, dir_ino, dir_inode);
            return 0;
        }
        buffer_release(bh);
    }

    uint32_t new_index = entry_count;
    uint32_t logical_block = new_index / entries_per_block;
    uint32_t entry_off = new_index % entries_per_block;
    uint32_t block_no;
    int inode_dirty = 0;
    if (noirfs_get_data_block(mnt, dir_inode, logical_block, 1, &block_no, &inode_dirty) != 0) {
        noirfs_dbg_puts("[noirfs] dir_add: get_data_block alloc failed");
        return -1;
    }
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        noirfs_dbg_puts("[noirfs] dir_add: buffer_get alloc failed");
        return -1;
    }
    struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
    entries[entry_off].ino = child_ino;
    cstring_copy(entries[entry_off].name, NOIRFS_NAME_MAX, name);
    buffer_mark_dirty(bh);
    buffer_release(bh);

    dir_inode->size = (new_index + 1) * entry_size;
    noirfs_write_inode_disk(mnt, dir_ino, dir_inode);
    noirfs_dbg_puts("[noirfs] dir_add end");
    return 0;
}

static int noirfs_dir_find_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, const char *name, struct noir_dirent_disk *entry_out, uint32_t *block_index, uint32_t *entry_index) {
    if (!mnt || !dir_inode || !name) {
        return -1;
    }
    const uint32_t entry_size = sizeof(struct noir_dirent_disk);
    const uint32_t entries_per_block = NOIRFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_inode->size / entry_size;

    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (noirfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
        struct noir_dirent_disk *entry = &entries[entry_off];
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

static int noirfs_dir_clear_entry(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode, uint32_t dir_ino, uint32_t block_no, uint32_t entry_off) {
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) {
        return -1;
    }
    struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
    entries[entry_off].ino = 0;
    entries[entry_off].name[0] = '\0';
    buffer_mark_dirty(bh);
    buffer_release(bh);
    return noirfs_write_inode_disk(mnt, dir_ino, dir_inode);
}

static int noirfs_dir_is_empty(struct noirfs_mount *mnt, struct noir_inode_disk *dir_inode) {
    const uint32_t entry_size = sizeof(struct noir_dirent_disk);
    const uint32_t entries_per_block = NOIRFS_BLOCK_SIZE / entry_size;
    uint32_t total_entries = dir_inode->size / entry_size;
    for (uint32_t i = 0; i < total_entries; ++i) {
        uint32_t logical_block = i / entries_per_block;
        uint32_t entry_off = i % entries_per_block;
        uint32_t block_no;
        int dirty = 0;
        if (noirfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0) {
            return -1;
        }
        if (block_no == 0) {
            continue;
        }
        struct buffer_head *bh = buffer_get(mnt->dev, block_no);
        if (!bh) {
            return -1;
        }
        struct noir_dirent_disk *entries = (struct noir_dirent_disk *)bh->data;
        struct noir_dirent_disk entry = entries[entry_off];
        buffer_release(bh);
        if (entry.ino != 0) {
            return 0;
        }
    }
    return 1;
}
