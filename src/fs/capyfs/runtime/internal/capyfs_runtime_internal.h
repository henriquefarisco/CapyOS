#ifndef CAPYFS_RUNTIME_INTERNAL_H
#define CAPYFS_RUNTIME_INTERNAL_H

#include "fs/capyfs.h"
#include "drivers/video/vga.h"
#include "fs/buffer.h"
#include "memory/kmem.h"

#define CAPYFS_DEBUG_CREATE 0

struct capyfs_inode {
  struct capyfs_mount *mount;
  uint32_t ino;
};

struct capyfs_mount {
  struct block_device *dev;
  struct capy_super super;
};

static inline void dbg_putc_serial(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}
static inline void dbg_hex32_serial(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4)
    dbg_putc_serial(hex[(value >> shift) & 0xFu]);
}
static inline uint32_t dbg_be32_serial_load(const uint8_t *src) {
  if (!src) return 0;
  return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}
static inline void store_u32_le_volatile(volatile uint8_t *dst, uint32_t value) {
  if (!dst) return;
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}
static inline void capyfs_dbg_puts(const char *msg) {
#if CAPYFS_DEBUG_CREATE
  if (msg) { vga_write(msg); vga_newline(); }
#else
  (void)msg;
#endif
}
static inline void capyfs_dbg_hex32(const char *label, uint32_t value) {
#if CAPYFS_DEBUG_CREATE
  static const char hex[] = "0123456789ABCDEF";
  char buf[11]; buf[0] = '0'; buf[1] = 'x';
  for (int i = 0; i < 8; ++i) buf[2 + i] = hex[(value >> (28 - i * 4)) & 0xF];
  buf[10] = '\0';
  if (label) vga_write(label);
  vga_write(buf); vga_newline();
#else
  (void)label; (void)value;
#endif
}
static inline struct capyfs_mount *inode_mount(struct inode *inode) {
  if (!inode || !inode->private_data) return NULL;
  return ((struct capyfs_inode *)inode->private_data)->mount;
}
static inline int names_equal(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] && b[i]) { if (a[i] != b[i]) return 0; ++i; }
  return a[i] == b[i];
}
static inline size_t cstring_length(const char *s) {
  size_t len = 0;
  while (s && s[len]) ++len;
  return len;
}
static inline void cstring_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || !dst_size) return;
  size_t i = 0;
  if (src) { for (; i < dst_size - 1 && src[i]; ++i) dst[i] = src[i]; }
  dst[i] = '\0';
}
static inline void memory_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  while (len--) *p++ = 0;
}
static inline int capyfs_format_finish(uint8_t *scratch, int scratch_heap, int rc) {
  if (scratch_heap && scratch) kfree(scratch);
  return rc;
}
static inline void zero_block(struct buffer_head *bh) {
  if (!bh) return;
  for (size_t i = 0; i < BUFFER_BLOCK_SIZE; ++i) bh->data[i] = 0;
  buffer_mark_dirty(bh);
}

long capyfs_read(struct file *file, void *buffer, size_t size);
long capyfs_write(struct file *file, const void *buffer, size_t size);
int capyfs_open(struct inode *inode, struct file *file);
int capyfs_close(struct file *file);

int capyfs_lookup(struct inode *dir, const char *name, struct inode **out_inode);
int capyfs_create(struct inode *dir, const char *name, uint16_t mode,
                  const struct vfs_metadata *meta, struct inode **out_inode);
int capyfs_iterate(struct inode *dir, vfs_iter_cb cb, void *ctx);
int capyfs_remove(struct inode *dir, const char *name, int is_dir);
int capyfs_rename_inode(struct inode *src_dir, const char *src_name,
                        struct inode *dst_dir, const char *dst_name);
int capyfs_stat_inode(struct inode *inode, struct vfs_stat *out);
int capyfs_set_metadata(struct inode *inode, const struct vfs_metadata *meta);

int capyfs_read_inode_disk(struct capyfs_mount *mnt, uint32_t ino,
                           struct capy_inode_disk *out);
int capyfs_write_inode_disk(struct capyfs_mount *mnt, uint32_t ino,
                            const struct capy_inode_disk *src);
struct inode *capyfs_create_vfs_inode(struct super_block *sb,
                                      struct capyfs_mount *mnt, uint32_t ino,
                                      const struct capy_inode_disk *disk);
int capyfs_alloc_inode(struct capyfs_mount *mnt, uint32_t *out_ino);
void capyfs_free_inode_bit(struct capyfs_mount *mnt, uint32_t ino);
void capyfs_free_block(struct capyfs_mount *mnt, uint32_t block);
int capyfs_get_data_block(struct capyfs_mount *mnt, struct capy_inode_disk *inode_disk,
                          uint32_t logical_block, int allocate,
                          uint32_t *out_block, int *inode_dirty);
int capyfs_alloc_block(struct capyfs_mount *mnt, uint32_t *out_block);
void capyfs_free_data_blocks(struct capyfs_mount *mnt,
                             struct capy_inode_disk *inode_disk);

int capyfs_dir_add_entry(struct capyfs_mount *mnt,
                         struct capy_inode_disk *dir_inode, uint32_t dir_ino,
                         uint32_t child_ino, const char *name);
int capyfs_dir_find_entry(struct capyfs_mount *mnt,
                          struct capy_inode_disk *dir_inode, const char *name,
                          struct capy_dirent_disk *entry_out,
                          uint32_t *block_index, uint32_t *entry_index);
int capyfs_dir_clear_entry(struct capyfs_mount *mnt,
                           struct capy_inode_disk *dir_inode, uint32_t dir_ino,
                           uint32_t block_no, uint32_t entry_off);
int capyfs_dir_is_empty(struct capyfs_mount *mnt, struct capy_inode_disk *dir_inode);

#endif
