#include "fs/journal.h"
#include "memory/kmem.h"
#include "security/crypt.h"
#include <stddef.h>

static const uint8_t JOURNAL_HMAC_DOMAIN[] = "CAPYJRNL2";

static void journal_zero_bytes(uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 0;
}

static int journal_constant_time_eq(const uint8_t *a, const uint8_t *b,
                                     size_t len) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0 ? 1 : 0;
}

/* Compute HMAC-SHA256 truncated to JOURNAL_HMAC_TAG_SIZE over a stable
 * layout binding header fields, refs and payload. The format is:
 *   "CAPYJRNL2" | u64(sequence) | u32(block_count) | refs[] | payload
 * Sequence + block_count are encoded little-endian to match the on-disk
 * representation written by the rest of the journal code. */
static void journal_compute_hmac(const struct journal *j, uint64_t sequence,
                                  uint32_t block_count,
                                  const struct journal_block_ref *refs,
                                  const uint8_t *payload, uint32_t payload_len,
                                  uint8_t out_tag[JOURNAL_HMAC_TAG_SIZE]) {
  uint8_t header_buf[sizeof(JOURNAL_HMAC_DOMAIN) + 8 + 4];
  size_t hpos = 0;
  for (size_t i = 0; i < sizeof(JOURNAL_HMAC_DOMAIN) - 1; i++) {
    header_buf[hpos++] = JOURNAL_HMAC_DOMAIN[i];
  }
  for (int i = 0; i < 8; i++) {
    header_buf[hpos++] = (uint8_t)((sequence >> (i * 8)) & 0xFFu);
  }
  for (int i = 0; i < 4; i++) {
    header_buf[hpos++] = (uint8_t)((block_count >> (i * 8)) & 0xFFu);
  }

  size_t refs_bytes = (size_t)block_count * sizeof(struct journal_block_ref);
  size_t total = hpos + refs_bytes + (size_t)payload_len;

  uint8_t *scratch = (uint8_t *)kmalloc(total);
  uint8_t mac[SHA256_DIGEST_SIZE];
  if (!scratch) {
    /* Out of memory: emit zero tag (replay will reject mismatch). */
    journal_zero_bytes(out_tag, JOURNAL_HMAC_TAG_SIZE);
    return;
  }
  size_t pos = 0;
  for (size_t i = 0; i < hpos; i++) scratch[pos++] = header_buf[i];
  if (refs_bytes && refs) {
    const uint8_t *src = (const uint8_t *)refs;
    for (size_t i = 0; i < refs_bytes; i++) scratch[pos++] = src[i];
  }
  if (payload_len && payload) {
    for (uint32_t i = 0; i < payload_len; i++) scratch[pos++] = payload[i];
  }

  crypt_hmac_sha256(j->hmac_key, j->hmac_key_len, scratch, total, mac);
  for (size_t i = 0; i < JOURNAL_HMAC_TAG_SIZE; i++) out_tag[i] = mac[i];
  journal_zero_bytes(scratch, total);
  kfree(scratch);
}

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

static int journal_format_with_version(struct journal *j,
                                        struct block_device *dev,
                                        uint64_t start_block,
                                        uint32_t block_count,
                                        uint32_t version) {
  if (!j || !dev || block_count < 4) return -1;

  j->dev = dev;
  j->start_block = start_block;
  j->block_count = block_count;

  j->sb.magic = JOURNAL_MAGIC;
  j->sb.version = version;
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

int journal_format(struct journal *j, struct block_device *dev,
                   uint64_t start_block, uint32_t block_count) {
  int r = journal_format_with_version(j, dev, start_block, block_count,
                                      JOURNAL_VERSION);
  if (r == 0 && j) {
    journal_zero_bytes(j->hmac_key, JOURNAL_HMAC_KEY_MAX);
    j->hmac_key_len = 0;
  }
  return r;
}

int journal_format_authenticated(struct journal *j, struct block_device *dev,
                                 uint64_t start_block, uint32_t block_count,
                                 const uint8_t *key, uint32_t key_len) {
  if (!key || key_len == 0 || key_len > JOURNAL_HMAC_KEY_MAX) return -1;
  int r = journal_format_with_version(j, dev, start_block, block_count,
                                      JOURNAL_VERSION_AUTH);
  if (r != 0) return r;
  journal_zero_bytes(j->hmac_key, JOURNAL_HMAC_KEY_MAX);
  for (uint32_t i = 0; i < key_len; i++) j->hmac_key[i] = key[i];
  j->hmac_key_len = key_len;
  return 0;
}

int journal_set_hmac_key(struct journal *j, const uint8_t *key,
                         uint32_t key_len) {
  if (!j) return -1;
  if (!key || key_len == 0) {
    journal_zero_bytes(j->hmac_key, JOURNAL_HMAC_KEY_MAX);
    j->hmac_key_len = 0;
    return 0;
  }
  if (key_len > JOURNAL_HMAC_KEY_MAX) return -1;
  journal_zero_bytes(j->hmac_key, JOURNAL_HMAC_KEY_MAX);
  for (uint32_t i = 0; i < key_len; i++) j->hmac_key[i] = key[i];
  j->hmac_key_len = key_len;
  return 0;
}

int journal_is_authenticated(const struct journal *j) {
  if (!j) return 0;
  return (j->sb.version == JOURNAL_VERSION_AUTH && j->hmac_key_len > 0) ? 1 : 0;
}

int journal_init(struct journal *j, struct block_device *dev,
                 uint64_t start_block, uint32_t block_count) {
  if (!j || !dev) return -1;

  j->dev = dev;
  j->start_block = start_block;
  j->block_count = block_count;
  j->dirty = 0;
  j->replaying = 0;
  journal_zero_bytes(j->hmac_key, JOURNAL_HMAC_KEY_MAX);
  j->hmac_key_len = 0;

  uint8_t *sb_buf = (uint8_t *)kmalloc(dev->block_size);
  if (!sb_buf) return -1;

  int r = journal_read_block(j, 0, sb_buf);
  if (r != 0) { kfree(sb_buf); return -1; }

  uint8_t *dst = (uint8_t *)&j->sb;
  for (size_t i = 0; i < sizeof(j->sb); i++) dst[i] = sb_buf[i];
  kfree(sb_buf);

  if (j->sb.magic != JOURNAL_MAGIC) return -1;
  if (j->sb.version != JOURNAL_VERSION &&
      j->sb.version != JOURNAL_VERSION_AUTH) {
    return -1;
  }

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

/* Helper: read 16 bytes of HMAC tag from a COMMIT block buffer.
 * Tag is stored at offset sizeof(journal_entry_header) within the block. */
static void journal_extract_commit_tag(const uint8_t *commit_block,
                                        uint8_t out_tag[JOURNAL_HMAC_TAG_SIZE]) {
  const uint8_t *src = commit_block + sizeof(struct journal_entry_header);
  for (size_t i = 0; i < JOURNAL_HMAC_TAG_SIZE; i++) out_tag[i] = src[i];
}

int journal_replay(struct journal *j) {
  if (!j || !journal_needs_replay(j)) return 0;
  /* Refuse to silently degrade an authenticated journal to a legacy replay
   * if no HMAC key is configured: that would let unauthenticated metadata
   * be applied to disk. Caller must set the key first. */
  if (j->sb.version == JOURNAL_VERSION_AUTH && j->hmac_key_len == 0) {
    return -1;
  }
  j->replaying = 1;
  int authenticated = journal_is_authenticated(j);

  uint32_t pos = j->sb.tail;
  uint8_t *buf = (uint8_t *)kmalloc(j->dev->block_size);
  if (!buf) { j->replaying = 0; return -1; }

  uint8_t *commit_buf = NULL;
  uint8_t *staged_payload = NULL;
  if (authenticated) {
    commit_buf = (uint8_t *)kmalloc(j->dev->block_size);
    staged_payload = (uint8_t *)kmalloc(
        (size_t)JOURNAL_ENTRY_MAX_BLOCKS * j->dev->block_size);
    if (!commit_buf || !staged_payload) {
      if (commit_buf) kfree(commit_buf);
      if (staged_payload) kfree(staged_payload);
      kfree(buf);
      j->replaying = 0;
      return -1;
    }
  }

  int integrity_ok = 1;

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
      uint32_t bs = j->dev->block_size;
      uint32_t blk_count = hdr->block_count;
      if (blk_count > JOURNAL_ENTRY_MAX_BLOCKS) blk_count = JOURNAL_ENTRY_MAX_BLOCKS;

      struct journal_block_ref refs_copy[JOURNAL_ENTRY_MAX_BLOCKS];
      const struct journal_block_ref *refs_src =
          (const struct journal_block_ref *)
              (buf + sizeof(struct journal_entry_header));
      for (uint32_t i = 0; i < blk_count; i++) refs_copy[i] = refs_src[i];

      uint32_t data_pos = (pos + 1) % j->sb.block_count;

      if (!authenticated) {
        /* Legacy v1 path: apply data immediately as we read each block. */
        for (uint32_t b = 0; b < blk_count && data_pos != j->sb.head; b++) {
          uint8_t *data_buf = (uint8_t *)kmalloc(bs);
          if (!data_buf) break;
          r = journal_read_block(j, 1 + data_pos, data_buf);
          if (r == 0) {
            block_device_write(j->dev, (uint32_t)refs_copy[b].target_block,
                               data_buf);
          }
          kfree(data_buf);
          data_pos = (data_pos + 1) % j->sb.block_count;
        }
        pos = data_pos;
        continue;
      }

      /* Authenticated path: stage payload, verify HMAC against the
       * matching COMMIT block before applying any write. */
      uint32_t staged_blocks = 0;
      int read_ok = 1;
      for (uint32_t b = 0; b < blk_count && data_pos != j->sb.head; b++) {
        r = journal_read_block(j, 1 + data_pos,
                               staged_payload + (size_t)b * bs);
        if (r != 0) { read_ok = 0; break; }
        staged_blocks++;
        data_pos = (data_pos + 1) % j->sb.block_count;
      }

      if (!read_ok || staged_blocks != blk_count) {
        integrity_ok = 0;
        break;
      }

      if (data_pos == j->sb.head) {
        /* No commit marker available — possible torn write. Refuse to
         * apply unauthenticated data. */
        integrity_ok = 0;
        break;
      }

      r = journal_read_block(j, 1 + data_pos, commit_buf);
      if (r != 0) { integrity_ok = 0; break; }

      struct journal_entry_header *commit_hdr =
          (struct journal_entry_header *)commit_buf;
      if (commit_hdr->magic != JOURNAL_MAGIC ||
          commit_hdr->type != JOURNAL_ENTRY_COMMIT ||
          commit_hdr->sequence != hdr->sequence ||
          (commit_hdr->flags & JOURNAL_ENTRY_FLAG_HMAC) == 0) {
        integrity_ok = 0;
        break;
      }

      uint8_t expected_tag[JOURNAL_HMAC_TAG_SIZE];
      uint8_t stored_tag[JOURNAL_HMAC_TAG_SIZE];
      journal_extract_commit_tag(commit_buf, stored_tag);

      uint32_t payload_len = staged_blocks * bs;
      journal_compute_hmac(j, hdr->sequence, blk_count, refs_copy,
                           staged_payload, payload_len, expected_tag);

      if (!journal_constant_time_eq(stored_tag, expected_tag,
                                     JOURNAL_HMAC_TAG_SIZE)) {
        integrity_ok = 0;
        break;
      }

      /* Authentic: apply each staged block to its target. */
      for (uint32_t b = 0; b < staged_blocks; b++) {
        block_device_write(j->dev, (uint32_t)refs_copy[b].target_block,
                           staged_payload + (size_t)b * bs);
      }

      data_pos = (data_pos + 1) % j->sb.block_count;
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

  if (commit_buf) kfree(commit_buf);
  if (staged_payload) {
    journal_zero_bytes(staged_payload,
                       (size_t)JOURNAL_ENTRY_MAX_BLOCKS * j->dev->block_size);
    kfree(staged_payload);
  }
  kfree(buf);
  j->replaying = 0;
  j->current_sequence = j->sb.sequence;
  return integrity_ok ? 0 : -1;
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

  /* If the journal is authenticated, compute HMAC over (sequence,
   * block_count, refs, payload) and embed it in the commit block.
   * Replay verifies this tag before applying any staged write. */
  if (journal_is_authenticated(j)) {
    uint8_t tag[JOURNAL_HMAC_TAG_SIZE];
    const uint8_t *payload = (txn->ref_count > 0) ? txn->data_buffer : NULL;
    uint32_t payload_len = (txn->ref_count > 0)
                               ? (uint32_t)(txn->ref_count * bs)
                               : 0u;
    journal_compute_hmac(j, txn->sequence, txn->ref_count, txn->refs,
                         payload, payload_len, tag);
    hdr->flags |= JOURNAL_ENTRY_FLAG_HMAC;
    uint8_t *tag_dst = buf + sizeof(struct journal_entry_header);
    for (size_t i = 0; i < JOURNAL_HMAC_TAG_SIZE; i++) tag_dst[i] = tag[i];
  }

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
