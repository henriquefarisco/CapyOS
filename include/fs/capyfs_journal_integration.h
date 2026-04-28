#ifndef FS_CAPYFS_JOURNAL_INTEGRATION_H
#define FS_CAPYFS_JOURNAL_INTEGRATION_H

#include <stdint.h>
#include "fs/block.h"

enum capyfs_journal_recovery_cause {
    CAPYFS_JOURNAL_RECOVERY_NONE = 0,
    CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY,
    CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY_FAILED,
    CAPYFS_JOURNAL_RECOVERY_FORMAT,
};

uint8_t capyfs_journal_last_recovery_cause(void);
const char *capyfs_journal_recovery_cause_label(uint8_t cause);

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
