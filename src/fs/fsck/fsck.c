#include "fs/fsck.h"
#include "memory/kmem.h"
#include <stddef.h>

static void fsck_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void fsck_add_error(struct fsck_result *r, enum fsck_error_type type,
                            uint32_t inode, uint32_t block, uint32_t detail) {
  if (r->error_count >= FSCK_MAX_ERRORS) return;
  struct fsck_error *e = &r->errors[r->error_count++];
  e->type = type;
  e->inode = inode;
  e->block = block;
  e->detail = detail;
}

static int fsck_read_block(struct block_device *dev, uint32_t block, void *buf) {
  if (!dev || !dev->ops || !dev->ops->read) return -1;
  return dev->ops->read(dev, block, 1, buf);
}

static int fsck_write_block(struct block_device *dev, uint32_t block,
                             const void *buf) {
  if (!dev || !dev->ops || !dev->ops->write) return -1;
  return dev->ops->write(dev, block, 1, buf);
}

int fsck_check(struct block_device *dev, struct fsck_result *result) {
  if (!dev || !result) return -1;
  fsck_memset(result, 0, sizeof(*result));
  result->clean = 1;

  uint8_t *buf = (uint8_t *)kmalloc(dev->block_size);
  if (!buf) return -1;

  /* Read and validate superblock */
  if (fsck_read_block(dev, 0, buf) != 0) {
    fsck_add_error(result, FSCK_ERR_BAD_SUPERBLOCK, 0, 0, 0);
    result->clean = 0;
    kfree(buf);
    return -1;
  }

  struct capy_super *sb = (struct capy_super *)buf;
  if (sb->magic != CAPYFS_MAGIC) {
    fsck_add_error(result, FSCK_ERR_BAD_MAGIC, 0, 0, sb->magic);
    result->clean = 0;
    kfree(buf);
    return -1;
  }

  if (sb->version != CAPYFS_VERSION) {
    fsck_add_error(result, FSCK_ERR_BAD_VERSION, 0, 0, sb->version);
    result->clean = 0;
  }

  uint32_t inode_count = sb->inode_count;
  uint32_t block_count = sb->block_count;
  uint32_t imap_start = sb->imap_start;
  uint32_t bmap_start = sb->bmap_start;
  uint32_t inode_start = sb->inode_start;
  uint32_t data_start = sb->data_start;

  /* Allocate tracking bitmaps */
  uint32_t inode_bytes = (inode_count + 7) / 8;
  uint32_t block_bytes = (block_count + 7) / 8;
  uint8_t *inode_seen = (uint8_t *)kmalloc(inode_bytes);
  uint8_t *block_seen = (uint8_t *)kmalloc(block_bytes);
  if (!inode_seen || !block_seen) {
    if (inode_seen) kfree(inode_seen);
    if (block_seen) kfree(block_seen);
    kfree(buf);
    return -1;
  }
  fsck_memset(inode_seen, 0, inode_bytes);
  fsck_memset(block_seen, 0, block_bytes);

  /* Read inode bitmap from disk */
  uint32_t imap_blocks = (inode_bytes + dev->block_size - 1) / dev->block_size;
  uint8_t *disk_imap = (uint8_t *)kmalloc(imap_blocks * dev->block_size);
  if (disk_imap) {
    for (uint32_t i = 0; i < imap_blocks; i++)
      fsck_read_block(dev, imap_start + i, disk_imap + i * dev->block_size);
  }

  /* Read block bitmap from disk */
  uint32_t bmap_blocks = (block_bytes + dev->block_size - 1) / dev->block_size;
  uint8_t *disk_bmap = (uint8_t *)kmalloc(bmap_blocks * dev->block_size);
  if (disk_bmap) {
    for (uint32_t i = 0; i < bmap_blocks; i++)
      fsck_read_block(dev, bmap_start + i, disk_bmap + i * dev->block_size);
  }

  /* Walk all inodes and track referenced blocks */
  uint32_t inodes_per_block = dev->block_size / sizeof(struct capy_inode_disk);
  uint32_t inode_block_count = (inode_count + inodes_per_block - 1) / inodes_per_block;

  for (uint32_t ib = 0; ib < inode_block_count; ib++) {
    if (fsck_read_block(dev, inode_start + ib, buf) != 0) continue;

    for (uint32_t j = 0; j < inodes_per_block; j++) {
      uint32_t ino = ib * inodes_per_block + j;
      if (ino >= inode_count) break;

      struct capy_inode_disk *inode =
        (struct capy_inode_disk *)(buf + j * sizeof(struct capy_inode_disk));

      /* Check if inode is marked allocated in bitmap */
      int bitmap_alloc = 0;
      if (disk_imap && ino / 8 < inode_bytes)
        bitmap_alloc = (disk_imap[ino / 8] >> (ino % 8)) & 1;

      int has_data = (inode->mode != 0 || inode->size != 0 || inode->links != 0);

      if (has_data) {
        inode_seen[ino / 8] |= (uint8_t)(1 << (ino % 8));

        if (!bitmap_alloc) {
          fsck_add_error(result, FSCK_ERR_ORPHAN_INODE, ino, 0, 0);
          result->orphan_inodes++;
          result->clean = 0;
        }

        if (inode->mode & 0x2) result->dirs_found++;
        else result->files_found++;
        result->inodes_used++;

        /* Track direct blocks */
        for (int d = 0; d < 12; d++) {
          uint32_t blk = inode->direct[d];
          if (blk == 0) continue;
          if (blk >= block_count) {
            fsck_add_error(result, FSCK_ERR_BAD_INODE_LINK, ino, blk, (uint32_t)d);
            result->clean = 0;
            continue;
          }
          uint32_t bidx = blk;
          if (block_seen[bidx / 8] & (1 << (bidx % 8))) {
            fsck_add_error(result, FSCK_ERR_CROSS_LINKED, ino, blk, 0);
            result->cross_linked++;
            result->clean = 0;
          } else {
            block_seen[bidx / 8] |= (uint8_t)(1 << (bidx % 8));
            result->blocks_used++;
          }
        }

        /* Track indirect block */
        if (inode->indirect != 0 && inode->indirect < block_count) {
          uint32_t ib2 = inode->indirect;
          block_seen[ib2 / 8] |= (uint8_t)(1 << (ib2 % 8));
          result->blocks_used++;
        }
      } else if (bitmap_alloc) {
        fsck_add_error(result, FSCK_ERR_DOUBLE_ALLOC_INODE, ino, 0, 0);
        result->clean = 0;
      }
    }
  }

  /* Check for orphan blocks (allocated in bitmap but not referenced) */
  if (disk_bmap) {
    for (uint32_t b = data_start; b < block_count; b++) {
      int bmap_alloc = (disk_bmap[b / 8] >> (b % 8)) & 1;
      int seen = (block_seen[b / 8] >> (b % 8)) & 1;
      if (bmap_alloc && !seen) {
        fsck_add_error(result, FSCK_ERR_ORPHAN_BLOCK, 0, b, 0);
        result->orphan_blocks++;
        result->clean = 0;
      }
    }
  }

  result->inodes_free = inode_count - result->inodes_used;
  result->blocks_free = block_count - result->blocks_used;

  /* Validate root inode */
  if (fsck_read_block(dev, inode_start, buf) == 0) {
    struct capy_inode_disk *root = (struct capy_inode_disk *)buf;
    if (!(root->mode & 0x2)) {
      fsck_add_error(result, FSCK_ERR_BAD_ROOT, 0, 0, root->mode);
      result->clean = 0;
    }
  }

  if (disk_imap) kfree(disk_imap);
  if (disk_bmap) kfree(disk_bmap);
  kfree(inode_seen);
  kfree(block_seen);
  kfree(buf);
  return 0;
}

int fsck_repair(struct block_device *dev, struct fsck_result *result) {
  if (!dev || !result) return -1;

  /* Rebuild bitmaps from actual inode references */
  uint8_t *buf = (uint8_t *)kmalloc(dev->block_size);
  if (!buf) return -1;

  if (fsck_read_block(dev, 0, buf) != 0) { kfree(buf); return -1; }
  struct capy_super *sb = (struct capy_super *)buf;

  uint32_t inode_count = sb->inode_count;
  uint32_t block_count = sb->block_count;
  uint32_t inode_bytes = (inode_count + 7) / 8;
  uint32_t block_bytes = (block_count + 7) / 8;

  uint8_t *new_imap = (uint8_t *)kmalloc(inode_bytes);
  uint8_t *new_bmap = (uint8_t *)kmalloc(block_bytes);
  if (!new_imap || !new_bmap) {
    if (new_imap) kfree(new_imap);
    if (new_bmap) kfree(new_bmap);
    kfree(buf);
    return -1;
  }
  fsck_memset(new_imap, 0, inode_bytes);
  fsck_memset(new_bmap, 0, block_bytes);

  /* Mark reserved blocks */
  for (uint32_t b = 0; b < sb->data_start && b < block_count; b++)
    new_bmap[b / 8] |= (uint8_t)(1 << (b % 8));

  /* Scan inodes */
  uint32_t inodes_per_block = dev->block_size / sizeof(struct capy_inode_disk);
  uint32_t inode_block_count = (inode_count + inodes_per_block - 1) / inodes_per_block;

  for (uint32_t ib = 0; ib < inode_block_count; ib++) {
    if (fsck_read_block(dev, sb->inode_start + ib, buf) != 0) continue;
    for (uint32_t j = 0; j < inodes_per_block; j++) {
      uint32_t ino = ib * inodes_per_block + j;
      if (ino >= inode_count) break;
      struct capy_inode_disk *inode =
        (struct capy_inode_disk *)(buf + j * sizeof(struct capy_inode_disk));
      if (inode->mode == 0 && inode->size == 0 && inode->links == 0) continue;

      new_imap[ino / 8] |= (uint8_t)(1 << (ino % 8));
      for (int d = 0; d < 12; d++) {
        if (inode->direct[d] && inode->direct[d] < block_count)
          new_bmap[inode->direct[d] / 8] |= (uint8_t)(1 << (inode->direct[d] % 8));
      }
      if (inode->indirect && inode->indirect < block_count)
        new_bmap[inode->indirect / 8] |= (uint8_t)(1 << (inode->indirect % 8));
    }
  }

  /* Write corrected bitmaps */
  uint32_t imap_blocks = (inode_bytes + dev->block_size - 1) / dev->block_size;
  for (uint32_t i = 0; i < imap_blocks; i++) {
    uint8_t *blk = (uint8_t *)kmalloc(dev->block_size);
    if (!blk) continue;
    fsck_memset(blk, 0, dev->block_size);
    uint32_t off = i * dev->block_size;
    uint32_t len = inode_bytes - off;
    if (len > dev->block_size) len = dev->block_size;
    for (uint32_t b = 0; b < len; b++) blk[b] = new_imap[off + b];
    fsck_write_block(dev, sb->imap_start + i, blk);
    kfree(blk);
  }

  uint32_t bmap_blocks = (block_bytes + dev->block_size - 1) / dev->block_size;
  for (uint32_t i = 0; i < bmap_blocks; i++) {
    uint8_t *blk = (uint8_t *)kmalloc(dev->block_size);
    if (!blk) continue;
    fsck_memset(blk, 0, dev->block_size);
    uint32_t off = i * dev->block_size;
    uint32_t len = block_bytes - off;
    if (len > dev->block_size) len = dev->block_size;
    for (uint32_t b = 0; b < len; b++) blk[b] = new_bmap[off + b];
    fsck_write_block(dev, sb->bmap_start + i, blk);
    kfree(blk);
  }

  kfree(new_imap);
  kfree(new_bmap);
  kfree(buf);
  result->repaired = 1;
  return 0;
}

static void fsck_itoa(uint32_t v, char *buf) {
  int p = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
  char tmp[12]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[p++] = tmp[i];
  buf[p] = 0;
}

void fsck_print_result(const struct fsck_result *result,
                        void (*print)(const char *)) {
  if (!result || !print) return;
  char num[16];

  print("=== fsck.CAPYFS result ===\n");
  print("Status: "); print(result->clean ? "CLEAN\n" : "ERRORS FOUND\n");

  print("Inodes: used="); fsck_itoa(result->inodes_used, num); print(num);
  print(" free="); fsck_itoa(result->inodes_free, num); print(num); print("\n");

  print("Blocks: used="); fsck_itoa(result->blocks_used, num); print(num);
  print(" free="); fsck_itoa(result->blocks_free, num); print(num); print("\n");

  print("Dirs: "); fsck_itoa(result->dirs_found, num); print(num);
  print(" Files: "); fsck_itoa(result->files_found, num); print(num); print("\n");

  if (result->error_count > 0) {
    print("Errors: "); fsck_itoa(result->error_count, num); print(num); print("\n");
    for (uint32_t i = 0; i < result->error_count; i++) {
      const struct fsck_error *e = &result->errors[i];
      const char *etype = "unknown";
      switch (e->type) {
        case FSCK_ERR_BAD_SUPERBLOCK: etype = "bad-superblock"; break;
        case FSCK_ERR_BAD_MAGIC: etype = "bad-magic"; break;
        case FSCK_ERR_BAD_VERSION: etype = "bad-version"; break;
        case FSCK_ERR_ORPHAN_INODE: etype = "orphan-inode"; break;
        case FSCK_ERR_ORPHAN_BLOCK: etype = "orphan-block"; break;
        case FSCK_ERR_DOUBLE_ALLOC_BLOCK: etype = "double-alloc-block"; break;
        case FSCK_ERR_DOUBLE_ALLOC_INODE: etype = "double-alloc-inode"; break;
        case FSCK_ERR_BAD_DIR_ENTRY: etype = "bad-dirent"; break;
        case FSCK_ERR_BAD_INODE_LINK: etype = "bad-link"; break;
        case FSCK_ERR_CROSS_LINKED: etype = "cross-linked"; break;
        case FSCK_ERR_BAD_ROOT: etype = "bad-root"; break;
        default: break;
      }
      print("  "); print(etype);
      print(" inode="); fsck_itoa(e->inode, num); print(num);
      print(" block="); fsck_itoa(e->block, num); print(num);
      print("\n");
    }
  }

  if (result->repaired) print("Repair: bitmaps rebuilt\n");
  print("=========================\n");
}
