#ifndef FS_JOURNAL_H
#define FS_JOURNAL_H

#include <stdint.h>
#include <stddef.h>
#include "fs/block.h"

#define JOURNAL_MAGIC 0x4A524E4C
#define JOURNAL_VERSION 1
#define JOURNAL_MAX_BLOCKS 256
#define JOURNAL_ENTRY_MAX_BLOCKS 16

enum journal_entry_type {
  JOURNAL_ENTRY_INVALID = 0,
  JOURNAL_ENTRY_CREATE,
  JOURNAL_ENTRY_DELETE,
  JOURNAL_ENTRY_RENAME,
  JOURNAL_ENTRY_WRITE_META,
  JOURNAL_ENTRY_TRUNCATE,
  JOURNAL_ENTRY_SETATTR,
  JOURNAL_ENTRY_COMMIT,
  JOURNAL_ENTRY_CHECKPOINT
};

struct journal_superblock {
  uint32_t magic;
  uint32_t version;
  uint32_t block_size;
  uint32_t block_count;
  uint32_t head;
  uint32_t tail;
  uint64_t sequence;
  uint32_t checksum;
  uint32_t reserved[7];
};

struct journal_entry_header {
  uint32_t magic;
  uint32_t type;
  uint64_t sequence;
  uint32_t block_count;
  uint32_t data_size;
  uint32_t checksum;
  uint32_t flags;
};

struct journal_block_ref {
  uint64_t target_block;
  uint32_t offset;
  uint32_t length;
};

struct journal {
  struct block_device *dev;
  uint64_t start_block;
  uint32_t block_count;
  struct journal_superblock sb;
  uint64_t current_sequence;
  int dirty;
  int replaying;
  uint8_t *buffer;
  size_t buffer_size;
};

struct journal_transaction {
  struct journal *journal;
  uint64_t sequence;
  struct journal_block_ref refs[JOURNAL_ENTRY_MAX_BLOCKS];
  uint32_t ref_count;
  uint8_t *data_buffer;
  uint32_t data_used;
  int committed;
};

int journal_init(struct journal *j, struct block_device *dev,
                 uint64_t start_block, uint32_t block_count);
int journal_format(struct journal *j, struct block_device *dev,
                   uint64_t start_block, uint32_t block_count);
int journal_replay(struct journal *j);
int journal_checkpoint(struct journal *j);

int journal_begin(struct journal *j, struct journal_transaction *txn);
int journal_log_block(struct journal_transaction *txn, uint64_t target_block,
                      const void *data, uint32_t offset, uint32_t length);
int journal_commit(struct journal_transaction *txn);
int journal_abort(struct journal_transaction *txn);

int journal_needs_replay(struct journal *j);
void journal_stats(struct journal *j, uint32_t *used, uint32_t *free);

#endif /* FS_JOURNAL_H */
