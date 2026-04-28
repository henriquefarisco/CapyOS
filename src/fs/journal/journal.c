#include "fs/journal.h"
#include "memory/kmem.h"
#include <stddef.h>

static uint32_t journal_crc32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
  }
  return ~crc;
}

static int journal_read_block(struct journal *j, uint64_t block, void *buf) {
  if (!j || !j->dev) return -1;
  return block_device_read(j->dev, (uint32_t)(j->start_block + block), buf);
}

static int journal_write_block(struct journal *j, uint64_t block,
                                const void *buf) {
  if (!j || !j->dev) return -1;
  return block_device_write(j->dev, (uint32_t)(j->start_block + block), buf);
}

int journal_format(struct journal *j, struct block_device *dev,
                   uint64_t start_block, uint32_t block_count) {
  if (!j || !dev || block_count < 4) return -1;

  j->dev = dev;
  j->start_block = start_block;
  j->block_count = block_count;

  j->sb.magic = JOURNAL_MAGIC;
  j->sb.version = JOURNAL_VERSION;
  j->sb.block_size = dev->block_size;
  j->sb.block_count = block_count - 1;
  j->sb.head = 0;
  j->sb.tail = 0;
  j->sb.sequence = 1;

  uint8_t *zero = (uint8_t *)kmalloc(dev->block_size);
  if (!zero) return -1;
  for (uint32_t i = 0; i < dev->block_size; i++) zero[i] = 0;

  for (uint32_t i = 0; i < block_count; i++) {
    journal_write_block(j, i, zero);
  }

  j->sb.checksum = journal_crc32(&j->sb, sizeof(j->sb) - sizeof(j->sb.checksum) -
                                  sizeof(j->sb.reserved));

  uint8_t *sb_buf = (uint8_t *)kmalloc(dev->block_size);
  if (!sb_buf) { kfree(zero); return -1; }
  for (uint32_t i = 0; i < dev->block_size; i++) sb_buf[i] = 0;

  uint8_t *src = (uint8_t *)&j->sb;
  for (size_t i = 0; i < sizeof(j->sb) && i < dev->block_size; i++)
    sb_buf[i] = src[i];

  int r = journal_write_block(j, 0, sb_buf);
  kfree(sb_buf);
  kfree(zero);

  j->current_sequence = j->sb.sequence;
  j->dirty = 0;
  j->replaying = 0;
  return r;
}

int journal_init(struct journal *j, struct block_device *dev,
                 uint64_t start_block, uint32_t block_count) {
  if (!j || !dev) return -1;

  j->dev = dev;
  j->start_block = start_block;
  j->block_count = block_count;
  j->dirty = 0;
  j->replaying = 0;

  uint8_t *sb_buf = (uint8_t *)kmalloc(dev->block_size);
  if (!sb_buf) return -1;

  int r = journal_read_block(j, 0, sb_buf);
  if (r != 0) { kfree(sb_buf); return -1; }

  uint8_t *dst = (uint8_t *)&j->sb;
  for (size_t i = 0; i < sizeof(j->sb); i++) dst[i] = sb_buf[i];
  kfree(sb_buf);

  if (j->sb.magic != JOURNAL_MAGIC || j->sb.version != JOURNAL_VERSION)
    return -1;

  j->current_sequence = j->sb.sequence;

  j->buffer_size = dev->block_size;
  j->buffer = (uint8_t *)kmalloc(j->buffer_size);
  if (!j->buffer) return -1;

  return 0;
}

int journal_needs_replay(struct journal *j) {
  if (!j) return 0;
  return (j->sb.head != j->sb.tail) ? 1 : 0;
}

int journal_replay(struct journal *j) {
  if (!j || !journal_needs_replay(j)) return 0;
  j->replaying = 1;

  uint32_t pos = j->sb.tail;
  uint8_t *buf = (uint8_t *)kmalloc(j->dev->block_size);
  if (!buf) { j->replaying = 0; return -1; }

  while (pos != j->sb.head) {
    int r = journal_read_block(j, 1 + pos, buf);
    if (r != 0) break;

    struct journal_entry_header *hdr = (struct journal_entry_header *)buf;
    if (hdr->magic != JOURNAL_MAGIC) break;

    if (hdr->type == JOURNAL_ENTRY_COMMIT) {
      pos = (pos + 1) % j->sb.block_count;
      continue;
    }

    if (hdr->type == JOURNAL_ENTRY_WRITE_META && hdr->block_count > 0) {
      uint32_t data_pos = (pos + 1) % j->sb.block_count;
      for (uint32_t b = 0; b < hdr->block_count && data_pos != j->sb.head; b++) {
        uint8_t *data_buf = (uint8_t *)kmalloc(j->dev->block_size);
        if (!data_buf) break;
        r = journal_read_block(j, 1 + data_pos, data_buf);
        if (r == 0) {
          struct journal_block_ref *ref =
            (struct journal_block_ref *)(buf + sizeof(struct journal_entry_header));
          if (b < JOURNAL_ENTRY_MAX_BLOCKS) {
            block_device_write(j->dev, (uint32_t)ref[b].target_block, data_buf);
          }
        }
        kfree(data_buf);
        data_pos = (data_pos + 1) % j->sb.block_count;
      }
      pos = data_pos;
    } else {
      pos = (pos + 1) % j->sb.block_count;
    }
  }

  j->sb.tail = j->sb.head;
  j->sb.sequence++;

  uint8_t *sb_buf = (uint8_t *)kmalloc(j->dev->block_size);
  if (sb_buf) {
    for (uint32_t i = 0; i < j->dev->block_size; i++) sb_buf[i] = 0;
    uint8_t *src = (uint8_t *)&j->sb;
    for (size_t i = 0; i < sizeof(j->sb); i++) sb_buf[i] = src[i];
    journal_write_block(j, 0, sb_buf);
    kfree(sb_buf);
  }

  kfree(buf);
  j->replaying = 0;
  j->current_sequence = j->sb.sequence;
  return 0;
}

int journal_begin(struct journal *j, struct journal_transaction *txn) {
  if (!j || !txn) return -1;
  txn->journal = j;
  txn->sequence = j->current_sequence++;
  txn->ref_count = 0;
  txn->data_buffer = NULL;
  txn->data_used = 0;
  txn->committed = 0;
  return 0;
}

int journal_log_block(struct journal_transaction *txn, uint64_t target_block,
                      const void *data, uint32_t offset, uint32_t length) {
  if (!txn || !data || txn->ref_count >= JOURNAL_ENTRY_MAX_BLOCKS) return -1;

  struct journal_block_ref *ref = &txn->refs[txn->ref_count];
  ref->target_block = target_block;
  ref->offset = offset;
  ref->length = length;
  txn->ref_count++;

  if (!txn->data_buffer) {
    txn->data_buffer = (uint8_t *)kmalloc(
      JOURNAL_ENTRY_MAX_BLOCKS * txn->journal->dev->block_size);
    if (!txn->data_buffer) return -1;
  }

  uint32_t bs = txn->journal->dev->block_size;
  uint8_t *dst = txn->data_buffer + (txn->ref_count - 1) * bs;
  const uint8_t *src = (const uint8_t *)data;
  for (uint32_t i = 0; i < bs; i++) dst[i] = (i < length) ? src[i] : 0;
  txn->data_used += bs;

  return 0;
}

int journal_commit(struct journal_transaction *txn) {
  if (!txn || !txn->journal || txn->committed) return -1;
  struct journal *j = txn->journal;

  uint32_t needed = 1 + txn->ref_count + 1;
  uint32_t used = (j->sb.head >= j->sb.tail)
    ? (j->sb.head - j->sb.tail)
    : (j->sb.block_count - j->sb.tail + j->sb.head);
  uint32_t avail = j->sb.block_count - used - 1;
  if (needed > avail) return -1;

  uint8_t *buf = (uint8_t *)kmalloc(j->dev->block_size);
  if (!buf) return -1;

  for (uint32_t i = 0; i < j->dev->block_size; i++) buf[i] = 0;
  struct journal_entry_header *hdr = (struct journal_entry_header *)buf;
  hdr->magic = JOURNAL_MAGIC;
  hdr->type = JOURNAL_ENTRY_WRITE_META;
  hdr->sequence = txn->sequence;
  hdr->block_count = txn->ref_count;
  hdr->data_size = txn->data_used;
  hdr->flags = 0;

  uint8_t *ref_dst = buf + sizeof(struct journal_entry_header);
  for (uint32_t i = 0; i < txn->ref_count; i++) {
    uint8_t *src = (uint8_t *)&txn->refs[i];
    for (size_t b = 0; b < sizeof(struct journal_block_ref); b++)
      ref_dst[i * sizeof(struct journal_block_ref) + b] = src[b];
  }

  hdr->checksum = journal_crc32(buf, j->dev->block_size);
  journal_write_block(j, 1 + j->sb.head, buf);
  j->sb.head = (j->sb.head + 1) % j->sb.block_count;

  uint32_t bs = j->dev->block_size;
  for (uint32_t i = 0; i < txn->ref_count; i++) {
    journal_write_block(j, 1 + j->sb.head, txn->data_buffer + i * bs);
    j->sb.head = (j->sb.head + 1) % j->sb.block_count;
  }

  for (uint32_t i = 0; i < j->dev->block_size; i++) buf[i] = 0;
  hdr->magic = JOURNAL_MAGIC;
  hdr->type = JOURNAL_ENTRY_COMMIT;
  hdr->sequence = txn->sequence;
  hdr->block_count = 0;
  hdr->data_size = 0;
  hdr->flags = 0;
  hdr->checksum = journal_crc32(buf, j->dev->block_size);
  journal_write_block(j, 1 + j->sb.head, buf);
  j->sb.head = (j->sb.head + 1) % j->sb.block_count;

  kfree(buf);
  if (txn->data_buffer) { kfree(txn->data_buffer); txn->data_buffer = NULL; }
  txn->committed = 1;
  j->dirty = 1;

  /* Persist updated head to superblock so replay survives a crash.
   * Without this, a re-init would see head == tail and skip replay. */
  {
    uint8_t *sb_buf = (uint8_t *)kmalloc(j->dev->block_size);
    if (sb_buf) {
      uint32_t i;
      uint8_t *src = (uint8_t *)&j->sb;
      for (i = 0; i < j->dev->block_size; i++) sb_buf[i] = 0;
      for (i = 0; i < sizeof(j->sb) && i < j->dev->block_size; i++)
        sb_buf[i] = src[i];
      journal_write_block(j, 0, sb_buf);
      kfree(sb_buf);
    }
  }
  return 0;
}

int journal_abort(struct journal_transaction *txn) {
  if (!txn) return -1;
  if (txn->data_buffer) { kfree(txn->data_buffer); txn->data_buffer = NULL; }
  txn->committed = 0;
  txn->ref_count = 0;
  return 0;
}

int journal_checkpoint(struct journal *j) {
  if (!j || !j->dirty) return 0;

  j->sb.tail = j->sb.head;
  j->sb.sequence++;
  j->sb.checksum = journal_crc32(&j->sb, sizeof(j->sb) - sizeof(j->sb.checksum) -
                                  sizeof(j->sb.reserved));

  uint8_t *sb_buf = (uint8_t *)kmalloc(j->dev->block_size);
  if (!sb_buf) return -1;
  for (uint32_t i = 0; i < j->dev->block_size; i++) sb_buf[i] = 0;
  uint8_t *src = (uint8_t *)&j->sb;
  for (size_t i = 0; i < sizeof(j->sb); i++) sb_buf[i] = src[i];
  int r = journal_write_block(j, 0, sb_buf);
  kfree(sb_buf);

  j->dirty = 0;
  j->current_sequence = j->sb.sequence;
  return r;
}

void journal_stats(struct journal *j, uint32_t *used, uint32_t *free_count) {
  if (!j) { if (used) *used = 0; if (free_count) *free_count = 0; return; }
  uint32_t u = (j->sb.head >= j->sb.tail)
    ? (j->sb.head - j->sb.tail)
    : (j->sb.block_count - j->sb.tail + j->sb.head);
  if (used) *used = u;
  if (free_count) *free_count = j->sb.block_count - u - 1;
}
