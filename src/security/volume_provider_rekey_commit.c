#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"

#define VP_REKEY_COMMIT_BLOCK_SIZE 4096u

static void vp_commit_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static int vp_commit_block_same(const uint8_t *a, const uint8_t *b,
                                size_t len) {
  uint8_t diff = 0u;
  if (!a || !b) return 0;
  for (size_t i = 0u; i < len; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0u;
}

static int vp_commit_checkpoint_same(
    const struct volume_provider_rekey_checkpoint *a,
    const struct volume_provider_rekey_checkpoint *b) {
  if (!a || !b) return 0;
  return a->magic0 == b->magic0 && a->magic1 == b->magic1 &&
         a->version == b->version && a->flags == b->flags &&
         a->phase_flags == b->phase_flags && a->plan_status == b->plan_status &&
         a->source_layout == b->source_layout &&
         a->target_layout == b->target_layout &&
         a->blocker_flags == b->blocker_flags &&
         a->raw_block_count == b->raw_block_count &&
         a->capyfs_block_count == b->capyfs_block_count &&
         a->source_first_lba == b->source_first_lba &&
         a->source_last_lba == b->source_last_lba &&
         a->target_first_lba == b->target_first_lba &&
         a->target_last_lba == b->target_last_lba &&
         a->scratch_first_lba == b->scratch_first_lba &&
         a->blocks_total == b->blocks_total &&
         a->blocks_completed == b->blocks_completed &&
         a->next_source_lba == b->next_source_lba &&
         a->next_target_lba == b->next_target_lba &&
         a->estimated_read_ops == b->estimated_read_ops &&
         a->estimated_write_ops == b->estimated_write_ops;
}

static int vp_commit_plan_matches_checkpoint(
    const struct volume_provider_rekey_plan *plan,
    const struct volume_provider_rekey_checkpoint *checkpoint) {
  return plan && checkpoint &&
         checkpoint->scratch_first_lba == plan->scratch_first_lba &&
         checkpoint->raw_block_count == plan->raw_block_count &&
         checkpoint->capyfs_block_count == plan->capyfs_block_count &&
         checkpoint->source_first_lba == plan->source_first_lba &&
         checkpoint->source_last_lba == plan->source_last_lba &&
         checkpoint->target_first_lba == plan->target_first_lba &&
         checkpoint->target_last_lba == plan->target_last_lba &&
         checkpoint->blocks_total == plan->blocks_to_reencrypt;
}

static int vp_commit_plan_matches_manifest(
    const struct volume_provider_rekey_plan *plan,
    const struct volume_provider_rekey_stage_manifest *manifest) {
  return plan && manifest && manifest->scratch_lba == plan->scratch_first_lba &&
         manifest->raw_block_count == plan->raw_block_count &&
         manifest->capyfs_block_count == plan->capyfs_block_count &&
         manifest->source_first_lba == plan->source_first_lba &&
         manifest->source_last_lba == plan->source_last_lba &&
         manifest->target_first_lba == plan->target_first_lba &&
         manifest->target_last_lba == plan->target_last_lba &&
         manifest->blocks_total == plan->blocks_to_reencrypt;
}

static uint32_t vp_commit_get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int vp_commit_capyfs_super_valid(const uint8_t *super,
                                        uint32_t expected_blocks) {
  uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t block_size = 0;
  uint32_t block_count = 0;
  uint32_t inode_count = 0;
  uint32_t bmap_start = 0;
  uint32_t imap_start = 0;
  uint32_t inode_start = 0;
  uint32_t data_start = 0;
  uint32_t block_bitmap_blocks = 0;
  uint32_t inode_bitmap_blocks = 0;
  uint32_t inode_table_blocks = 0;

  if (!super || expected_blocks == 0u) return 0;
  magic = vp_commit_get_u32_le(super + 0);
  version = vp_commit_get_u32_le(super + 4);
  block_size = vp_commit_get_u32_le(super + 8);
  block_count = vp_commit_get_u32_le(super + 12);
  inode_count = vp_commit_get_u32_le(super + 16);
  bmap_start = vp_commit_get_u32_le(super + 20);
  imap_start = vp_commit_get_u32_le(super + 24);
  inode_start = vp_commit_get_u32_le(super + 28);
  data_start = vp_commit_get_u32_le(super + 32);
  if (magic != CAPYFS_MAGIC || version != CAPYFS_VERSION ||
      block_size != CAPYFS_BLOCK_SIZE || block_count != expected_blocks ||
      inode_count == 0u || bmap_start != 1u) {
    return 0;
  }
  block_bitmap_blocks = (block_count + bits_per_block - 1u) / bits_per_block;
  inode_bitmap_blocks = (inode_count + bits_per_block - 1u) / bits_per_block;
  inode_table_blocks =
      (inode_count * (uint32_t)sizeof(struct capy_inode_disk) +
       CAPYFS_BLOCK_SIZE - 1u) /
      CAPYFS_BLOCK_SIZE;
  if (imap_start != bmap_start + block_bitmap_blocks ||
      inode_start != imap_start + inode_bitmap_blocks ||
      data_start != inode_start + inode_table_blocks) {
    return 0;
  }
  return data_start < block_count;
}

int volume_provider_rekey_execute_commit_header(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_plan plan;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_checkpoint updated_checkpoint;
  struct volume_provider_rekey_checkpoint verified_checkpoint;
  struct volume_provider_rekey_stage_manifest manifest;
  struct volume_provider_rekey_stage_manifest verified_manifest;
  struct capyos_volume_header header;
  struct capyos_volume_header verified_header;
  struct block_device *opened_crypt = NULL;
  uint8_t scratch_block[VP_REKEY_COMMIT_BLOCK_SIZE];
  uint8_t commit_block[VP_REKEY_COMMIT_BLOCK_SIZE];
  uint8_t verify_block[VP_REKEY_COMMIT_BLOCK_SIZE];
  uint8_t opened_super[VP_REKEY_COMMIT_BLOCK_SIZE];
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];

  if (out) vp_commit_wipe(out, sizeof(*out));
  if (!out) return -1;
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  if (!chunked_4096 || !password ||
      chunked_4096->block_size != VP_REKEY_COMMIT_BLOCK_SIZE) {
    return -1;
  }

  vp_commit_wipe(&plan, sizeof(plan));
  vp_commit_wipe(&checkpoint, sizeof(checkpoint));
  vp_commit_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_commit_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_commit_wipe(&manifest, sizeof(manifest));
  vp_commit_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_commit_wipe(&header, sizeof(header));
  vp_commit_wipe(&verified_header, sizeof(verified_header));
  vp_commit_wipe(scratch_block, sizeof(scratch_block));
  vp_commit_wipe(commit_block, sizeof(commit_block));
  vp_commit_wipe(verify_block, sizeof(verify_block));
  vp_commit_wipe(opened_super, sizeof(opened_super));
  vp_commit_wipe(key1, sizeof(key1));
  vp_commit_wipe(key2, sizeof(key2));

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_commit_wipe(out, sizeof(*out));
    goto fail_commit;
  }

  out->flags = flags;
  out->plan_status = plan.status;
  out->blocker_flags = plan.blocker_flags;
  out->blocks_to_reencrypt = plan.blocks_to_reencrypt;
  out->scratch_first_lba = plan.scratch_first_lba;
  out->estimated_read_ops = plan.estimated_read_ops;
  out->estimated_write_ops = plan.estimated_write_ops;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN;

  if (plan.status == VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED;
    goto ok_commit;
  }
  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    if (plan.scratch_first_lba >= chunked_4096->block_count) {
      out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    }
    goto ok_commit;
  }

  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;

  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        scratch_block) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint) != 0 ||
      volume_provider_rekey_stage_manifest_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &manifest) != 0 ||
      capyos_volume_header_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
          &header) != CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED;
    goto ok_commit;
  }
  if (!vp_commit_plan_matches_checkpoint(&plan, &checkpoint) ||
      !vp_commit_plan_matches_manifest(&plan, &manifest) ||
      checkpoint.flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS ||
      checkpoint.blocks_completed != checkpoint.blocks_total ||
      manifest.checkpoint_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) ||
      manifest.staged_header_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE) ||
      (checkpoint.phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) ==
          0u ||
      (checkpoint.phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE) ==
          0u ||
      (checkpoint.phase_flags &
       (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED;
    goto ok_commit;
  }
  if (header.kdf_algo_id != CAPYOS_VOLUME_KDF_ALGO_ARGON2ID ||
      header.data_offset_lba != plan.target_first_lba) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED;
    goto ok_commit;
  }
  if (capyos_volume_header_derive_keys(&header, password, key1, key2) !=
      CAPYOS_VOLUME_HEADER_OK ||
      capyos_volume_header_serialize(&header, commit_block) !=
          CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED;
    goto ok_commit;
  }

  if (block_device_write(chunked_4096, 0u, commit_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITE_FAILED;
    goto ok_commit;
  }
  if (block_device_read(chunked_4096, 0u, verify_block) != 0 ||
      !vp_commit_block_same(commit_block, verify_block, sizeof(commit_block)) ||
      capyos_volume_header_parse(verify_block, &verified_header) !=
          CAPYOS_VOLUME_HEADER_OK ||
      capyos_volume_header_derive_keys(&verified_header, password, key1, key2) !=
          CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_VERIFY_FAILED;
    goto ok_commit;
  }
  if (volume_provider_open(chunked_4096, password, legacy_salt, legacy_salt_len,
                           legacy_iter, &opened_crypt) != 0 || !opened_crypt) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_OPEN_FAILED;
    goto ok_commit;
  }
  if (block_device_read(opened_crypt, 0u, opened_super) != 0 ||
      !vp_commit_capyfs_super_valid(opened_super, checkpoint.blocks_total)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_OPEN_FAILED;
    goto ok_commit;
  }
  crypt_free(opened_crypt);
  opened_crypt = NULL;

  updated_checkpoint = checkpoint;
  updated_checkpoint.flags = VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED;
  updated_checkpoint.phase_flags =
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;
  updated_checkpoint.next_source_lba = checkpoint.source_first_lba;
  updated_checkpoint.next_target_lba = checkpoint.target_first_lba;
  if (volume_provider_rekey_checkpoint_serialize(
          &updated_checkpoint,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_CHECKPOINT_FAILED;
    goto ok_commit;
  }
  manifest.checkpoint_crc32 = capyos_volume_header_crc32(
      scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
      VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE);
  if (volume_provider_rekey_stage_manifest_serialize(
          &manifest,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE) != 0 ||
      block_device_write(chunked_4096, plan.scratch_first_lba,
                         scratch_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_CHECKPOINT_FAILED;
    goto ok_commit;
  }
  vp_commit_wipe(verify_block, sizeof(verify_block));
  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        verify_block) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          verify_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &verified_checkpoint) != 0 ||
      !vp_commit_checkpoint_same(&updated_checkpoint, &verified_checkpoint) ||
      volume_provider_rekey_stage_manifest_parse(
          verify_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &verified_manifest) != 0 ||
      !vp_commit_plan_matches_manifest(&plan, &verified_manifest) ||
      verified_manifest.checkpoint_crc32 !=
          capyos_volume_header_crc32(
              verify_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) ||
      verified_manifest.staged_header_crc32 !=
          capyos_volume_header_crc32(
              verify_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_CHECKPOINT_FAILED;
    goto ok_commit;
  }

  out->blocks_completed = updated_checkpoint.blocks_completed;
  out->blocks_remaining = 0u;
  out->next_source_lba = updated_checkpoint.next_source_lba;
  out->next_target_lba = updated_checkpoint.next_target_lba;
  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITTEN;

ok_commit:
  if (opened_crypt) crypt_free(opened_crypt);
  vp_commit_wipe(&plan, sizeof(plan));
  vp_commit_wipe(&checkpoint, sizeof(checkpoint));
  vp_commit_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_commit_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_commit_wipe(&manifest, sizeof(manifest));
  vp_commit_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_commit_wipe(&header, sizeof(header));
  vp_commit_wipe(&verified_header, sizeof(verified_header));
  vp_commit_wipe(scratch_block, sizeof(scratch_block));
  vp_commit_wipe(commit_block, sizeof(commit_block));
  vp_commit_wipe(verify_block, sizeof(verify_block));
  vp_commit_wipe(opened_super, sizeof(opened_super));
  vp_commit_wipe(key1, sizeof(key1));
  vp_commit_wipe(key2, sizeof(key2));
  return 0;

fail_commit:
  vp_commit_wipe(&plan, sizeof(plan));
  vp_commit_wipe(&checkpoint, sizeof(checkpoint));
  vp_commit_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_commit_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_commit_wipe(&manifest, sizeof(manifest));
  vp_commit_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_commit_wipe(&header, sizeof(header));
  vp_commit_wipe(&verified_header, sizeof(verified_header));
  vp_commit_wipe(scratch_block, sizeof(scratch_block));
  vp_commit_wipe(commit_block, sizeof(commit_block));
  vp_commit_wipe(verify_block, sizeof(verify_block));
  vp_commit_wipe(opened_super, sizeof(opened_super));
  vp_commit_wipe(key1, sizeof(key1));
  vp_commit_wipe(key2, sizeof(key2));
  return -1;
}
