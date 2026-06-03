#ifndef FS_FSCK_GEOMETRY_H
#define FS_FSCK_GEOMETRY_H

#include <stdint.h>
#include "fs/capyfs.h"

/*
 * Pure, host-testable validator for the CAPYFS superblock geometry,
 * extracted so fsck can fail closed on a corrupted or hostile
 * superblock BEFORE deriving bitmap sizes or walking inodes.
 *
 * Hardening (2026-05-29): fsck reads the superblock from an untrusted
 * on-disk image (validating that image is literally a checker's job)
 * and then derives, with uint32 arithmetic:
 *
 *   inode_bytes      = (inode_count + 7) / 8
 *   block_bytes      = (block_count + 7) / 8
 *   inode_block_count= (inode_count + inodes_per_block - 1) / ipb
 *   imap_blocks      = (inode_bytes + bs - 1) / bs ; alloc imap_blocks * bs
 *   bmap_blocks      = (block_bytes + bs - 1) / bs ; alloc bmap_blocks * bs
 *
 * A superblock with inode_count/block_count near UINT32_MAX wraps the
 * (+7), (+ipb-1) and (* bs) steps, producing an UNDERSIZED allocation
 * that the subsequent inode/block walk then writes past — a heap
 * overflow driven entirely by attacker-controlled on-disk metadata.
 *
 * This predicate replays the exact arithmetic in uint64 and rejects
 * (fail-closed, returns 0) any superblock whose:
 *   - block_size does not match the device it is checked on;
 *   - counts are zero or exceed the physical device capacity;
 *   - layout offsets fall outside the device;
 *   - derived sizes would overflow uint32 or not fit the device.
 * All intermediate math is uint64 so the validator cannot itself
 * overflow. NULL sb returns 0. Pure: no I/O, no allocation.
 */
int fsck_super_geometry_valid(const struct capy_super *sb,
                              uint32_t dev_block_count,
                              uint32_t dev_block_size);

#endif /* FS_FSCK_GEOMETRY_H */
