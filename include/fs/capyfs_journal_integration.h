#ifndef FS_CAPYFS_JOURNAL_INTEGRATION_H
#define FS_CAPYFS_JOURNAL_INTEGRATION_H

#include <stdint.h>
#include "fs/block.h"

int capyfs_journal_mount_hook(struct block_device *dev, uint32_t data_start);
int capyfs_journal_write_metadata(struct block_device *dev,
                                   uint32_t target_block,
                                   const void *data,
                                   uint32_t data_len);
int capyfs_journal_checkpoint_hook(struct block_device *dev);
int capyfs_journal_active(struct block_device *dev);
void capyfs_journal_stats(struct block_device *dev, uint32_t *used,
                           uint32_t *free_count);

#endif /* FS_CAPYFS_JOURNAL_INTEGRATION_H */
