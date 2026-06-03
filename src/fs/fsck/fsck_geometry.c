#include "fs/fsck_geometry.h"
#include <stddef.h>

int fsck_super_geometry_valid(const struct capy_super *sb,
                              uint32_t dev_block_count,
                              uint32_t dev_block_size) {
  const uint64_t u32_max = 0xFFFFFFFFu;

  if (!sb) return 0;
  if (dev_block_count == 0u) return 0;
  /* Need a sane block size; fsck derives every size from the device's
   * block size, and the on-disk superblock must agree with it. */
  if (dev_block_size < 8u) return 0;
  if (sb->block_size != dev_block_size) return 0;

  uint64_t bs = (uint64_t)dev_block_size;
  uint64_t devbc = (uint64_t)dev_block_count;
  uint64_t inode_count = (uint64_t)sb->inode_count;
  uint64_t block_count = (uint64_t)sb->block_count;

  if (inode_count == 0u || block_count == 0u) return 0;

  /* Counts must fit the physical device. */
  if (block_count > devbc) return 0;
  uint64_t ipb = bs / (uint64_t)sizeof(struct capy_inode_disk);
  if (ipb == 0u) return 0;
  if (inode_count > block_count * ipb) return 0;

  /* Layout offsets must land inside the device; data_start also bounds
   * the orphan-block scan (b in [data_start, block_count)). */
  if ((uint64_t)sb->imap_start >= devbc) return 0;
  if ((uint64_t)sb->bmap_start >= devbc) return 0;
  if ((uint64_t)sb->inode_start >= devbc) return 0;
  if ((uint64_t)sb->data_start > block_count) return 0;

  /* Replay fsck's uint32 arithmetic in uint64 and reject if any step
   * would wrap a uint32. */
  if (inode_count + 7u > u32_max) return 0;
  if (block_count + 7u > u32_max) return 0;
  if (inode_count + (ipb - 1u) > u32_max) return 0; /* inode_block_count step */

  uint64_t inode_bytes = (inode_count + 7u) / 8u;
  uint64_t block_bytes = (block_count + 7u) / 8u;
  if (inode_bytes + (bs - 1u) > u32_max) return 0;
  if (block_bytes + (bs - 1u) > u32_max) return 0;

  uint64_t imap_blocks = (inode_bytes + bs - 1u) / bs;
  uint64_t bmap_blocks = (block_bytes + bs - 1u) / bs;
  if (imap_blocks * bs > u32_max) return 0; /* kmalloc(imap_blocks * bs) */
  if (bmap_blocks * bs > u32_max) return 0; /* kmalloc(bmap_blocks * bs) */

  /* Bitmap regions must fit on the device at their declared offsets. */
  if ((uint64_t)sb->imap_start + imap_blocks > devbc) return 0;
  if ((uint64_t)sb->bmap_start + bmap_blocks > devbc) return 0;

  return 1;
}
