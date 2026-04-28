/*
 * CAPYFS Journal Integration Layer
 *
 * Provides journal replay on mount and journaled metadata write helpers
 * that wrap the existing CAPYFS operations with write-ahead logging.
 *
 * The journal occupies CAPYFS_JOURNAL_BLOCKS blocks immediately after
 * the data_start region. When formatting, these blocks are reserved.
 * On mount, if the journal has uncommitted entries, they are replayed
 * before the filesystem becomes usable.
 */
#include "fs/capyfs.h"
#include "fs/capyfs_journal_integration.h"
#include "fs/journal.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"

#include <stddef.h>
#include <stdint.h>

#define CAPYFS_JOURNAL_BLOCKS 32

static uint8_t g_journal_recovery_cause = CAPYFS_JOURNAL_RECOVERY_NONE;

uint8_t capyfs_journal_last_recovery_cause(void) {
    return g_journal_recovery_cause;
}

const char *capyfs_journal_recovery_cause_label(uint8_t cause) {
    switch ((enum capyfs_journal_recovery_cause)cause) {
    case CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY:        return "wal-replay";
    case CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY_FAILED: return "wal-replay-failed";
    case CAPYFS_JOURNAL_RECOVERY_FORMAT:            return "first-mount-format";
    default:                                        return "none";
    }
}

/* Per-mount journal state. Stored alongside the capyfs_mount in sb->fs_private
 * area. Since we cannot change the internal capyfs_mount struct without
 * touching the core capyfs.c (which has CRLF), we keep a side table. */

#define CAPYFS_JOURNAL_MAX_MOUNTS 4

struct capyfs_journal_slot {
    struct block_device *dev;
    struct journal jrnl;
    int active;
};

static struct capyfs_journal_slot g_journal_slots[CAPYFS_JOURNAL_MAX_MOUNTS];

static struct capyfs_journal_slot *journal_slot_for(struct block_device *dev) {
    if (!dev) return NULL;
    for (int i = 0; i < CAPYFS_JOURNAL_MAX_MOUNTS; i++) {
        if (g_journal_slots[i].active && g_journal_slots[i].dev == dev)
            return &g_journal_slots[i];
    }
    return NULL;
}

static struct capyfs_journal_slot *journal_slot_alloc(struct block_device *dev) {
    for (int i = 0; i < CAPYFS_JOURNAL_MAX_MOUNTS; i++) {
        if (!g_journal_slots[i].active) {
            g_journal_slots[i].dev = dev;
            g_journal_slots[i].active = 1;
            return &g_journal_slots[i];
        }
    }
    return NULL;
}

/*
 * capyfs_journal_mount_hook - Called after mount_capyfs succeeds.
 * Initializes the journal for this device and replays if needed.
 *
 * The journal lives at block (data_start - CAPYFS_JOURNAL_BLOCKS).
 * This requires that format reserved those blocks.
 *
 * Returns 0 on success (including "no journal found, skipping"),
 * negative on fatal error.
 */
int capyfs_journal_mount_hook(struct block_device *dev, uint32_t data_start) {
    g_journal_recovery_cause = CAPYFS_JOURNAL_RECOVERY_NONE;

    if (!dev || data_start < CAPYFS_JOURNAL_BLOCKS + 2) {
        klog(KLOG_INFO, "[capyfs-journal] No journal region available, skipping.");
        return 0;
    }

    struct capyfs_journal_slot *slot = journal_slot_alloc(dev);
    if (!slot) {
        klog(KLOG_WARN, "[capyfs-journal] No free journal slot.");
        return 0;
    }

    uint64_t journal_start = (uint64_t)(data_start - CAPYFS_JOURNAL_BLOCKS);

    int rc = journal_init(&slot->jrnl, dev, journal_start, CAPYFS_JOURNAL_BLOCKS);
    if (rc != 0) {
        /* Journal not formatted yet — first mount after upgrade. Format it. */
        klog(KLOG_INFO, "[capyfs-journal] Formatting journal region.");
        rc = journal_format(&slot->jrnl, dev, journal_start, CAPYFS_JOURNAL_BLOCKS);
        if (rc != 0) {
            klog(KLOG_WARN, "[capyfs-journal] Journal format failed.");
            slot->active = 0;
            return 0; /* non-fatal: FS works without journal */
        }
        g_journal_recovery_cause = CAPYFS_JOURNAL_RECOVERY_FORMAT;
        return 0;
    }

    if (journal_needs_replay(&slot->jrnl)) {
        klog(KLOG_INFO, "[capyfs-journal] Replaying journal after unclean shutdown.");
        rc = journal_replay(&slot->jrnl);
        if (rc != 0) {
            klog(KLOG_ERROR, "[capyfs-journal] Journal replay failed!");
            g_journal_recovery_cause = CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY_FAILED;
        } else {
            klog(KLOG_INFO, "[capyfs-journal] Journal replay completed.");
            g_journal_recovery_cause = CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY;
        }
    } else {
        klog(KLOG_DEBUG, "[capyfs-journal] Journal clean, no replay needed.");
    }

    return 0;
}

/*
 * capyfs_journal_write_metadata - Log a metadata block write to the journal
 * before actually writing it. This is the WAL guarantee: if we crash between
 * the journal write and the actual write, replay will redo it.
 */
int capyfs_journal_write_metadata(struct block_device *dev,
                                   uint32_t target_block,
                                   const void *data,
                                   uint32_t data_len) {
    struct capyfs_journal_slot *slot = journal_slot_for(dev);
    if (!slot) {
        /* No journal active — write directly (legacy behavior) */
        return 0;
    }

    struct journal_transaction txn;
    if (journal_begin(&slot->jrnl, &txn) != 0) {
        return -1;
    }

    if (journal_log_block(&txn, (uint64_t)target_block, data, 0, data_len) != 0) {
        journal_abort(&txn);
        return -1;
    }

    if (journal_commit(&txn) != 0) {
        return -1;
    }

    return 0;
}

/*
 * capyfs_journal_checkpoint_hook - Called on explicit sync (do-sync).
 * Advances the journal tail to free space.
 */
int capyfs_journal_checkpoint_hook(struct block_device *dev) {
    struct capyfs_journal_slot *slot = journal_slot_for(dev);
    if (!slot) return 0;
    return journal_checkpoint(&slot->jrnl);
}

/*
 * capyfs_journal_status - Returns 1 if journal is active for this device.
 */
int capyfs_journal_active(struct block_device *dev) {
    struct capyfs_journal_slot *slot = journal_slot_for(dev);
    return (slot != NULL) ? 1 : 0;
}

void capyfs_journal_stats(struct block_device *dev, uint32_t *used,
                           uint32_t *free_count) {
    struct capyfs_journal_slot *slot = journal_slot_for(dev);
    if (!slot) {
        if (used) *used = 0;
        if (free_count) *free_count = 0;
        return;
    }
    journal_stats(&slot->jrnl, used, free_count);
}
