#include "internal/capyfs_runtime_internal.h"

int capyfs_lookup(struct inode *dir, const char *name, struct inode **out_inode) {
  capyfs_dbg_puts("[CAPYFS] lookup begin");
  if (dir) capyfs_dbg_hex32("   dir ino=", dir->ino);
  if (!dir || !out_inode || !name) return -1;
  if ((dir->mode & VFS_MODE_DIR) == 0) return -1;
  struct capyfs_mount *mnt = inode_mount(dir);
  if (!mnt) return -1;

  struct capy_inode_disk dir_disk;
  if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) return -1;
  dir->size = dir_disk.size;

  struct capy_dirent_disk found;
  if (capyfs_dir_find_entry(mnt, &dir_disk, name, &found, NULL, NULL) != 0) return -1;

  struct capy_inode_disk child_disk;
  if (capyfs_read_inode_disk(mnt, found.ino, &child_disk) != 0) return -1;

  struct inode *child = capyfs_create_vfs_inode(dir->sb, mnt, found.ino, &child_disk);
  if (!child) return -1;
  *out_inode = child;
  return 0;
}

int capyfs_create(struct inode *dir, const char *name, uint16_t mode,
                  const struct vfs_metadata *meta, struct inode **out_inode) {
  capyfs_dbg_puts("[CAPYFS] create begin");
  capyfs_dbg_hex32("   dir ptr=", (uint32_t)(uintptr_t)dir);
  if (!dir || !name || !out_inode) return -1;
  if ((dir->mode & VFS_MODE_DIR) == 0) return -1;
  size_t name_len = cstring_length(name);
  if (name_len == 0 || name_len >= CAPYFS_NAME_MAX) return -1;

  struct capyfs_mount *mnt = inode_mount(dir);
  if (!mnt) return -1;

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
  for (size_t i = 0; i < 12; ++i) new_disk.direct[i] = 0;
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

int capyfs_create_pub(struct inode *dir, const char *name, uint16_t mode,
                      const struct vfs_metadata *meta, struct inode **out_inode) {
  return capyfs_create(dir, name, mode, meta, out_inode);
}

int capyfs_remove(struct inode *dir, const char *name, int is_dir) {
  if (!dir || !name) return -1;
  if ((dir->mode & VFS_MODE_DIR) == 0) return -1;
  struct capyfs_mount *mnt = inode_mount(dir);
  if (!mnt) return -1;
  struct capy_inode_disk dir_disk;
  if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) return -1;

  struct capy_dirent_disk entry;
  uint32_t block_no = 0;
  uint32_t entry_off = 0;
  if (capyfs_dir_find_entry(mnt, &dir_disk, name, &entry, &block_no, &entry_off) != 0)
    return -1;

  struct capy_inode_disk target_disk;
  if (capyfs_read_inode_disk(mnt, entry.ino, &target_disk) != 0) return -1;
  int target_is_dir = (target_disk.mode & VFS_MODE_DIR) != 0;
  if (is_dir) {
    if (!target_is_dir) return -1;
    int empty = capyfs_dir_is_empty(mnt, &target_disk);
    if (empty != 1) return -1;
  } else if (target_is_dir) {
    return -1;
  }

  capyfs_free_data_blocks(mnt, &target_disk);
  if (capyfs_dir_clear_entry(mnt, &dir_disk, dir->ino, block_no, entry_off) != 0)
    return -1;

  struct capy_inode_disk zero_disk;
  memory_zero(&zero_disk, sizeof(zero_disk));
  if (capyfs_write_inode_disk(mnt, entry.ino, &zero_disk) != 0) return -1;
  capyfs_free_inode_bit(mnt, entry.ino);
  dir->size = dir_disk.size;
  return 0;
}

int capyfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx) {
  if (!dir || !cb) return -1;
  if ((dir->mode & VFS_MODE_DIR) == 0) return -1;
  struct capyfs_mount *mnt = inode_mount(dir);
  if (!mnt) return -1;
  struct capy_inode_disk dir_disk;
  if (capyfs_read_inode_disk(mnt, dir->ino, &dir_disk) != 0) return -1;
  dir->size = dir_disk.size;
  const uint32_t entry_size = sizeof(struct capy_dirent_disk);
  const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
  uint32_t total_entries = dir_disk.size / entry_size;

  for (uint32_t i = 0; i < total_entries; ++i) {
    uint32_t logical_block = i / entries_per_block;
    uint32_t entry_off = i % entries_per_block;
    uint32_t block_no;
    int dirty = 0;
    if (capyfs_get_data_block(mnt, &dir_disk, logical_block, 0, &block_no, &dirty) != 0)
      return -1;
    if (block_no == 0) continue;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) return -1;
    struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
    struct capy_dirent_disk entry = entries[entry_off];
    buffer_release(bh);
    if (entry.ino == 0 || entry.name[0] == '\0') continue;
    struct capy_inode_disk child_disk;
    if (capyfs_read_inode_disk(mnt, entry.ino, &child_disk) != 0) return -1;
    if (cb(entry.name, child_disk.mode, ctx) != 0) break;
  }
  return 0;
}

int capyfs_rename_inode(struct inode *src_dir, const char *src_name,
                        struct inode *dst_dir, const char *dst_name) {
  if (!src_dir || !dst_dir || !src_name || !dst_name) return -1;
  if ((src_dir->mode & VFS_MODE_DIR) == 0 || (dst_dir->mode & VFS_MODE_DIR) == 0)
    return -1;
  struct capyfs_mount *src_mnt = inode_mount(src_dir);
  struct capyfs_mount *dst_mnt = inode_mount(dst_dir);
  if (!src_mnt || !dst_mnt || src_mnt != dst_mnt) return -1;

  struct capy_inode_disk src_disk;
  struct capy_inode_disk dst_disk;
  if (capyfs_read_inode_disk(src_mnt, src_dir->ino, &src_disk) != 0) return -1;
  if (capyfs_read_inode_disk(dst_mnt, dst_dir->ino, &dst_disk) != 0) return -1;

  struct capy_dirent_disk entry;
  uint32_t src_block = 0;
  uint32_t src_off = 0;
  if (capyfs_dir_find_entry(src_mnt, &src_disk, src_name, &entry, &src_block, &src_off) != 0)
    return -1;

  if (src_dir->ino == dst_dir->ino) {
    if (names_equal(src_name, dst_name)) return 0;
    struct buffer_head *bh = buffer_get(src_mnt->dev, src_block);
    if (!bh) return -1;
    struct capy_dirent_disk *ents = (struct capy_dirent_disk *)bh->data;
    cstring_copy(ents[src_off].name, CAPYFS_NAME_MAX, dst_name);
    buffer_mark_dirty(bh);
    buffer_release(bh);
    return capyfs_write_inode_disk(src_mnt, src_dir->ino, &src_disk);
  }

  struct capy_dirent_disk existing;
  if (capyfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, NULL, NULL) == 0)
    return -1;

  if (capyfs_dir_add_entry(dst_mnt, &dst_disk, dst_dir->ino, entry.ino, dst_name) != 0)
    return -1;
  dst_dir->size = dst_disk.size;

  if (capyfs_dir_clear_entry(src_mnt, &src_disk, src_dir->ino, src_block, src_off) != 0) {
    uint32_t dst_block = 0;
    uint32_t dst_off = 0;
    if (capyfs_dir_find_entry(dst_mnt, &dst_disk, dst_name, &existing, &dst_block, &dst_off) == 0)
      capyfs_dir_clear_entry(dst_mnt, &dst_disk, dst_dir->ino, dst_block, dst_off);
    return -1;
  }
  src_dir->size = src_disk.size;
  return 0;
}

int capyfs_stat_inode(struct inode *inode, struct vfs_stat *out) {
  if (!inode || !out) return -1;
  out->ino = inode->ino;
  out->size = inode->size;
  out->uid = inode->uid;
  out->gid = inode->gid;
  out->mode = inode->mode;
  out->perm = inode->perm;
  return 0;
}

int capyfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta) {
  if (!inode || !meta) return -1;
  struct capyfs_mount *mnt = inode_mount(inode);
  if (!mnt) return -1;
  struct capy_inode_disk disk;
  if (capyfs_read_inode_disk(mnt, inode->ino, &disk) != 0) return -1;
  disk.uid = meta->uid;
  disk.gid = meta->gid;
  disk.perm = meta->perm;
  if (capyfs_write_inode_disk(mnt, inode->ino, &disk) != 0) return -1;
  inode->uid = disk.uid;
  inode->gid = disk.gid;
  inode->perm = disk.perm;
  return 0;
}
