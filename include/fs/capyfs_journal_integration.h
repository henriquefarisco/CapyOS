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

/* Kernel root secret used to derive per-volume HMAC keys for the journal.
 * The kernel runtime should call install_root_secret() once with the
 * post-unlock active volume key (or another stable per-system secret) so
 * journals on this system can use authenticated mode. clear() wipes it. */
void capyfs_journal_install_root_secret(const uint8_t *secret, uint32_t len);
void capyfs_journal_clear_root_secret(void);
int  capyfs_journal_root_secret_installed(void);

/* Mount hook. Pass `super_bytes`/`super_len` for the volume superblock so
 * the integration layer can derive a per-volume HMAC key when a root secret
 * is installed; pass NULL/0 to keep legacy (v1) behaviour. */
int capyfs_journal_mount_hook(struct block_device *dev, uint32_t data_start,
                              const void *super_bytes, uint32_t super_len);
int capyfs_journal_write_metadata(struct block_device *dev,
                                   uint32_t target_block,
                                   const void *data,
                                   uint32_t data_len);
int capyfs_journal_checkpoint_hook(struct block_device *dev);
int capyfs_journal_active(struct block_device *dev);
void capyfs_journal_stats(struct block_device *dev, uint32_t *used,
                           uint32_t *free_count);

/* Release the per-mount journal slot bound to `dev`. Intended for graceful
 * unmount paths and for tests that exercise multiple synthetic devices: the
 * slot table has a fixed capacity and once a slot is allocated it stays
 * bound to the original `dev` pointer until released. Calling on an
 * unknown device is a no-op. */
void capyfs_journal_release_slot(struct block_device *dev);

#endif /* FS_CAPYFS_JOURNAL_INTEGRATION_H */
