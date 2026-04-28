#include "internal/capyfs_runtime_internal.h"

int capyfs_dir_add_entry(struct capyfs_mount *mnt,
                         struct capy_inode_disk *dir_inode, uint32_t dir_ino,
                         uint32_t child_ino, const char *name) {
  if (!mnt || !dir_inode || !name) return -1;
  capyfs_dbg_puts("[CAPYFS] dir_add begin");
  capyfs_dbg_hex32("   dir_ino=", dir_ino);
  capyfs_dbg_hex32("   child_ino=", child_ino);
  size_t name_len = cstring_length(name);
  if (name_len == 0 || name_len >= CAPYFS_NAME_MAX) return -1;

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
    if (block_no == 0) continue;
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

int capyfs_dir_find_entry(struct capyfs_mount *mnt,
                          struct capy_inode_disk *dir_inode, const char *name,
                          struct capy_dirent_disk *entry_out,
                          uint32_t *block_index, uint32_t *entry_index) {
  if (!mnt || !dir_inode || !name) return -1;
  const uint32_t entry_size = sizeof(struct capy_dirent_disk);
  const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
  uint32_t total_entries = dir_inode->size / entry_size;

  for (uint32_t i = 0; i < total_entries; ++i) {
    uint32_t logical_block = i / entries_per_block;
    uint32_t entry_off = i % entries_per_block;
    uint32_t block_no;
    int dirty = 0;
    if (capyfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0)
      return -1;
    if (block_no == 0) continue;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) return -1;
    struct capy_dirent_disk *ents = (struct capy_dirent_disk *)bh->data;
    struct capy_dirent_disk *entry = &ents[entry_off];
    if (entry->ino != 0 && names_equal(entry->name, name)) {
      if (entry_out) *entry_out = *entry;
      if (block_index) *block_index = block_no;
      if (entry_index) *entry_index = entry_off;
      buffer_release(bh);
      return 0;
    }
    buffer_release(bh);
  }
  return -1;
}

int capyfs_dir_clear_entry(struct capyfs_mount *mnt,
                           struct capy_inode_disk *dir_inode, uint32_t dir_ino,
                           uint32_t block_no, uint32_t entry_off) {
  struct buffer_head *bh = buffer_get(mnt->dev, block_no);
  if (!bh) return -1;
  struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
  entries[entry_off].ino = 0;
  entries[entry_off].name[0] = '\0';
  buffer_mark_dirty(bh);
  buffer_release(bh);
  return capyfs_write_inode_disk(mnt, dir_ino, dir_inode);
}

int capyfs_dir_is_empty(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode) {
  const uint32_t entry_size = sizeof(struct capy_dirent_disk);
  const uint32_t entries_per_block = CAPYFS_BLOCK_SIZE / entry_size;
  uint32_t total_entries = dir_inode->size / entry_size;
  for (uint32_t i = 0; i < total_entries; ++i) {
    uint32_t logical_block = i / entries_per_block;
    uint32_t entry_off = i % entries_per_block;
    uint32_t block_no;
    int dirty = 0;
    if (capyfs_get_data_block(mnt, dir_inode, logical_block, 0, &block_no, &dirty) != 0)
      return -1;
    if (block_no == 0) continue;
    struct buffer_head *bh = buffer_get(mnt->dev, block_no);
    if (!bh) return -1;
    struct capy_dirent_disk *entries = (struct capy_dirent_disk *)bh->data;
    struct capy_dirent_disk entry = entries[entry_off];
    buffer_release(bh);
    if (entry.ino != 0) return 0;
  }
  return 1;
}
