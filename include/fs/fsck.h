#ifndef FS_FSCK_H
#define FS_FSCK_H

#include <stdint.h>
#include <stddef.h>
#include "fs/block.h"
#include "fs/capyfs.h"

#define FSCK_MAX_ERRORS 64

enum fsck_error_type {
  FSCK_ERR_NONE = 0,
  FSCK_ERR_BAD_SUPERBLOCK,
  FSCK_ERR_BAD_MAGIC,
  FSCK_ERR_BAD_VERSION,
  FSCK_ERR_BAD_INODE_BITMAP,
  FSCK_ERR_BAD_BLOCK_BITMAP,
  FSCK_ERR_ORPHAN_INODE,
  FSCK_ERR_ORPHAN_BLOCK,
  FSCK_ERR_DOUBLE_ALLOC_BLOCK,
  FSCK_ERR_DOUBLE_ALLOC_INODE,
  FSCK_ERR_BAD_DIR_ENTRY,
  FSCK_ERR_BAD_INODE_LINK,
  FSCK_ERR_CROSS_LINKED,
  FSCK_ERR_BAD_ROOT
};

struct fsck_error {
  enum fsck_error_type type;
  uint32_t inode;
  uint32_t block;
  uint32_t detail;
};

struct fsck_result {
  int clean;
  uint32_t error_count;
  struct fsck_error errors[FSCK_MAX_ERRORS];
  uint32_t inodes_used;
  uint32_t inodes_free;
  uint32_t blocks_used;
  uint32_t blocks_free;
  uint32_t dirs_found;
  uint32_t files_found;
  uint32_t orphan_inodes;
  uint32_t orphan_blocks;
  uint32_t cross_linked;
  int repaired;
};

int fsck_check(struct block_device *dev, struct fsck_result *result);
int fsck_repair(struct block_device *dev, struct fsck_result *result);
void fsck_print_result(const struct fsck_result *result, void (*print)(const char *));

#endif /* FS_FSCK_H */
