#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"

#define VP_REKEY_RECOVERY_BLOCK_SIZE 4096u

struct vp_rekey_recovery_offset_view {
  struct block_device *lower;
  uint32_t start_lba;
  uint32_t block_count;
};

static void vp_recovery_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static uint32_t vp_recovery_get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int vp_recovery_block_same(const uint8_t *a, const uint8_t *b,
                                  size_t len) {
  uint8_t diff = 0u;
  if (!a || !b) return 0;
  for (size_t i = 0u; i < len; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0u;
}

static int vp_recovery_block_zero(const uint8_t *block, size_t len) {
  uint8_t diff = 0u;
  if (!block) return 0;
  for (size_t i = 0u; i < len; ++i) {
    diff |= block[i];
  }
  return diff == 0u;
}

static int vp_recovery_offset_read(void *ctx, uint32_t block_no,
                                   void *buffer) {
  struct vp_rekey_recovery_offset_view *view =
      (struct vp_rekey_recovery_offset_view *)ctx;
  if (!view || !view->lower || !buffer || block_no >= view->block_count) {
    return -1;
  }
  return block_device_read(view->lower, view->start_lba + block_no, buffer);
}

static int vp_recovery_offset_write(void *ctx, uint32_t block_no,
                                    const void *buffer) {
  struct vp_rekey_recovery_offset_view *view =
      (struct vp_rekey_recovery_offset_view *)ctx;
  if (!view || !view->lower || !buffer || block_no >= view->block_count) {
    return -1;
  }
  return block_device_write(view->lower, view->start_lba + block_no, buffer);
}

static const struct block_device_ops vp_recovery_offset_ops = {
    .read_block = vp_recovery_offset_read,
    .write_block = vp_recovery_offset_write,
};

static int vp_recovery_checkpoint_same(
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

static int vp_recovery_plan_matches_checkpoint(
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

static int vp_recovery_plan_matches_manifest(
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

static int vp_recovery_scratch_integrity_valid(
    const uint8_t *scratch_block,
    const struct volume_provider_rekey_stage_manifest *manifest) {
  return scratch_block && manifest &&
         manifest->checkpoint_crc32 ==
             capyos_volume_header_crc32(
                 scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                 VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) &&
         manifest->staged_header_crc32 ==
             capyos_volume_header_crc32(
                 scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
                 VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE);
}

static int vp_recovery_zero_scratch(struct block_device *dev, uint32_t lba,
                                    uint32_t *status) {
  uint8_t block[VP_REKEY_RECOVERY_BLOCK_SIZE];
  int rc = -1;
  vp_recovery_wipe(block, sizeof(block));
  if (block_device_write(dev, lba, block) != 0) {
    if (status) {
      *status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED;
    }
    goto out;
  }
  vp_recovery_wipe(block, sizeof(block));
  if (block_device_read(dev, lba, block) != 0 ||
      !vp_recovery_block_zero(block, sizeof(block))) {
    if (status) {
      *status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED;
    }
    goto out;
  }
  rc = 0;
out:
  vp_recovery_wipe(block, sizeof(block));
  return rc;
}

static int vp_recovery_capyfs_super_blocks(const uint8_t *super,
                                           uint32_t visible_blocks,
                                           uint32_t *out_blocks) {
  uint32_t bits_per_block = CAPYFS_BLOCK_SIZE * 8u;
  uint32_t block_count = 0;
  uint32_t inode_count = 0;
  uint32_t bmap_start = 0;
  uint32_t imap_start = 0;
  uint32_t inode_start = 0;
  uint32_t data_start = 0;
  uint32_t block_bitmap_blocks = 0;
  uint32_t inode_bitmap_blocks = 0;
  uint32_t inode_table_blocks = 0;
  if (out_blocks) *out_blocks = 0u;
  if (!super || !out_blocks || visible_blocks == 0u) return 0;
  block_count = vp_recovery_get_u32_le(super + 12);
  inode_count = vp_recovery_get_u32_le(super + 16);
  bmap_start = vp_recovery_get_u32_le(super + 20);
  imap_start = vp_recovery_get_u32_le(super + 24);
  inode_start = vp_recovery_get_u32_le(super + 28);
  data_start = vp_recovery_get_u32_le(super + 32);
  if (vp_recovery_get_u32_le(super + 0) != CAPYFS_MAGIC ||
      vp_recovery_get_u32_le(super + 4) != CAPYFS_VERSION ||
      vp_recovery_get_u32_le(super + 8) != CAPYFS_BLOCK_SIZE ||
      block_count == 0u || block_count > visible_blocks || inode_count == 0u ||
      bmap_start != 1u) {
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
      data_start != inode_start + inode_table_blocks || data_start >= block_count) {
    return 0;
  }
  *out_blocks = block_count;
  return 1;
}

int volume_provider_rekey_execute_rollback_step(
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
  struct vp_rekey_recovery_offset_view target_view_ctx;
  struct block_device target_view;
  struct block_device *legacy_crypt = NULL;
  struct block_device *target_crypt = NULL;
  uint8_t scratch_block[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t plain[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t verify[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t legacy_key1[CRYPT_KEY_SIZE];
  uint8_t legacy_key2[CRYPT_KEY_SIZE];
  uint8_t target_key1[CRYPT_KEY_SIZE];
  uint8_t target_key2[CRYPT_KEY_SIZE];
  uint32_t restore_lba = 0u;
  uint32_t logical_lba = 0u;
  uint32_t remaining = 0u;
  uint32_t scratch_status = 0u;

  if (out) vp_recovery_wipe(out, sizeof(*out));
  if (!out) return -1;
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  if (!chunked_4096 || !password ||
      chunked_4096->block_size != VP_REKEY_RECOVERY_BLOCK_SIZE) {
    return -1;
  }

  vp_recovery_wipe(&plan, sizeof(plan));
  vp_recovery_wipe(&checkpoint, sizeof(checkpoint));
  vp_recovery_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_recovery_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_recovery_wipe(&manifest, sizeof(manifest));
  vp_recovery_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_recovery_wipe(&header, sizeof(header));
  vp_recovery_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_recovery_wipe(&target_view, sizeof(target_view));
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));
  vp_recovery_wipe(plain, sizeof(plain));
  vp_recovery_wipe(verify, sizeof(verify));
  vp_recovery_wipe(legacy_key1, sizeof(legacy_key1));
  vp_recovery_wipe(legacy_key2, sizeof(legacy_key2));
  vp_recovery_wipe(target_key1, sizeof(target_key1));
  vp_recovery_wipe(target_key2, sizeof(target_key2));

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_recovery_wipe(out, sizeof(*out));
    goto fail_rollback;
  }

  out->flags = flags;
  out->plan_status = plan.status;
  out->blocker_flags = plan.blocker_flags;
  out->blocks_to_reencrypt = plan.blocks_to_reencrypt;
  out->scratch_first_lba = plan.scratch_first_lba;
  out->estimated_read_ops = plan.estimated_read_ops;
  out->estimated_write_ops = plan.estimated_write_ops;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE;

  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = plan.status == VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED
                      ? VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED
                      : VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    goto ok_rollback;
  }
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
          &header) != CAPYOS_VOLUME_HEADER_OK ||
      !vp_recovery_scratch_integrity_valid(scratch_block, &manifest) ||
      !vp_recovery_plan_matches_checkpoint(&plan, &checkpoint) ||
      !vp_recovery_plan_matches_manifest(&plan, &manifest) ||
      checkpoint.flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS ||
      header.kdf_algo_id != CAPYOS_VOLUME_KDF_ALGO_ARGON2ID ||
      header.data_offset_lba != plan.target_first_lba ||
      (checkpoint.phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) == 0u ||
      (checkpoint.phase_flags &
       (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_BUILD_FAILED;
    goto ok_rollback;
  }

  if (checkpoint.blocks_completed <= 1u) {
    if (vp_recovery_zero_scratch(chunked_4096, plan.scratch_first_lba,
                                 &scratch_status) != 0) {
      out->status = scratch_status;
      goto ok_rollback;
    }
    out->blocks_completed = 0u;
    out->blocks_remaining = 0u;
    out->next_source_lba = checkpoint.source_last_lba;
    out->next_target_lba = checkpoint.target_last_lba;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE;
    goto ok_rollback;
  }

  if (capyos_volume_header_derive_keys(&header, password, target_key1,
                                       target_key2) != CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_BUILD_FAILED;
    goto ok_rollback;
  }
  crypt_derive_xts_keys(password, legacy_salt, legacy_salt_len, legacy_iter,
                        legacy_key1, legacy_key2);
  target_view_ctx.lower = chunked_4096;
  target_view_ctx.start_lba = checkpoint.target_first_lba;
  target_view_ctx.block_count = checkpoint.blocks_total;
  target_view.name = "rekey-rollback-target";
  target_view.block_size = chunked_4096->block_size;
  target_view.block_count = checkpoint.blocks_total;
  target_view.ctx = &target_view_ctx;
  target_view.ops = &vp_recovery_offset_ops;
  legacy_crypt = crypt_init(chunked_4096, legacy_key1, legacy_key2);
  target_crypt = crypt_init(&target_view, target_key1, target_key2);
  if (!legacy_crypt || !target_crypt) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_BUILD_FAILED;
    goto ok_rollback;
  }

  restore_lba = checkpoint.source_last_lba -
                (checkpoint.blocks_completed - 2u);
  if (restore_lba < checkpoint.source_first_lba ||
      restore_lba > checkpoint.source_last_lba) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_BUILD_FAILED;
    goto ok_rollback;
  }
  logical_lba = restore_lba - checkpoint.source_first_lba;
  if (block_device_read(target_crypt, logical_lba, plain) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_READ_FAILED;
    goto ok_rollback;
  }
  if (block_device_write(legacy_crypt, restore_lba, plain) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_WRITE_FAILED;
    goto ok_rollback;
  }
  if (block_device_read(legacy_crypt, restore_lba, verify) != 0 ||
      !vp_recovery_block_same(plain, verify, sizeof(plain))) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_VERIFY_FAILED;
    goto ok_rollback;
  }

  remaining = checkpoint.blocks_completed - 1u;
  if (remaining == 0u) {
    if (vp_recovery_zero_scratch(chunked_4096, plan.scratch_first_lba,
                                 &scratch_status) != 0) {
      out->status = scratch_status;
      goto ok_rollback;
    }
    out->blocks_completed = 0u;
    out->blocks_remaining = 0u;
    out->next_source_lba = checkpoint.source_last_lba;
    out->next_target_lba = checkpoint.target_last_lba;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE;
    goto ok_rollback;
  }

  updated_checkpoint = checkpoint;
  updated_checkpoint.blocks_completed = remaining;
  updated_checkpoint.flags = VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS;
  updated_checkpoint.phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                                   VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                                   VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                                   VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE;
  updated_checkpoint.next_source_lba = checkpoint.source_last_lba - remaining;
  updated_checkpoint.next_target_lba = checkpoint.target_last_lba - remaining;
  if (volume_provider_rekey_checkpoint_serialize(
          &updated_checkpoint,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED;
    goto ok_rollback;
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
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED;
    goto ok_rollback;
  }
  vp_recovery_wipe(verify, sizeof(verify));
  if (block_device_read(chunked_4096, plan.scratch_first_lba, verify) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          verify + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &verified_checkpoint) != 0 ||
      !vp_recovery_checkpoint_same(&updated_checkpoint, &verified_checkpoint) ||
      volume_provider_rekey_stage_manifest_parse(
          verify + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &verified_manifest) != 0 ||
      !vp_recovery_scratch_integrity_valid(verify, &verified_manifest)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED;
    goto ok_rollback;
  }

  out->blocks_completed = remaining;
  out->blocks_remaining = checkpoint.blocks_total - remaining;
  out->next_source_lba = updated_checkpoint.next_source_lba;
  out->next_target_lba = updated_checkpoint.next_target_lba;
  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_STEP_DONE;

ok_rollback:
  if (target_crypt) crypt_free(target_crypt);
  if (legacy_crypt) crypt_free(legacy_crypt);
  vp_recovery_wipe(&plan, sizeof(plan));
  vp_recovery_wipe(&checkpoint, sizeof(checkpoint));
  vp_recovery_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_recovery_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_recovery_wipe(&manifest, sizeof(manifest));
  vp_recovery_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_recovery_wipe(&header, sizeof(header));
  vp_recovery_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_recovery_wipe(&target_view, sizeof(target_view));
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));
  vp_recovery_wipe(plain, sizeof(plain));
  vp_recovery_wipe(verify, sizeof(verify));
  vp_recovery_wipe(legacy_key1, sizeof(legacy_key1));
  vp_recovery_wipe(legacy_key2, sizeof(legacy_key2));
  vp_recovery_wipe(target_key1, sizeof(target_key1));
  vp_recovery_wipe(target_key2, sizeof(target_key2));
  return 0;

fail_rollback:
  vp_recovery_wipe(&plan, sizeof(plan));
  vp_recovery_wipe(&checkpoint, sizeof(checkpoint));
  vp_recovery_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_recovery_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_recovery_wipe(&manifest, sizeof(manifest));
  vp_recovery_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_recovery_wipe(&header, sizeof(header));
  vp_recovery_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_recovery_wipe(&target_view, sizeof(target_view));
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));
  vp_recovery_wipe(plain, sizeof(plain));
  vp_recovery_wipe(verify, sizeof(verify));
  vp_recovery_wipe(legacy_key1, sizeof(legacy_key1));
  vp_recovery_wipe(legacy_key2, sizeof(legacy_key2));
  vp_recovery_wipe(target_key1, sizeof(target_key1));
  vp_recovery_wipe(target_key2, sizeof(target_key2));
  return -1;
}

int volume_provider_rekey_execute_cleanup_scratch(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_stage_manifest manifest;
  struct block_device *opened_crypt = NULL;
  uint8_t header_block[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t super[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint8_t scratch_block[VP_REKEY_RECOVERY_BLOCK_SIZE];
  uint32_t capyfs_blocks = 0u;
  uint32_t scratch_lba = 0u;

  if (out) vp_recovery_wipe(out, sizeof(*out));
  if (!out) return -1;
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  if (!chunked_4096 || !password ||
      chunked_4096->block_size != VP_REKEY_RECOVERY_BLOCK_SIZE) {
    return -1;
  }

  vp_recovery_wipe(&checkpoint, sizeof(checkpoint));
  vp_recovery_wipe(&manifest, sizeof(manifest));
  vp_recovery_wipe(header_block, sizeof(header_block));
  vp_recovery_wipe(super, sizeof(super));
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));

  out->flags = flags;
  if (block_device_read(chunked_4096, 0u, header_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_BUILD_FAILED;
    goto ok_cleanup;
  }
  if (!capyos_volume_header_looks_valid(header_block)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_NOT_NEEDED;
    goto ok_cleanup;
  }
  if (volume_provider_open(chunked_4096, password, legacy_salt, legacy_salt_len,
                           legacy_iter, &opened_crypt) != 0 || !opened_crypt ||
      block_device_read(opened_crypt, 0u, super) != 0 ||
      !vp_recovery_capyfs_super_blocks(super, opened_crypt->block_count,
                                       &capyfs_blocks)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_BUILD_FAILED;
    goto ok_cleanup;
  }
  scratch_lba = capyfs_blocks + 1u;
  out->scratch_first_lba = scratch_lba;
  if (scratch_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_NOT_NEEDED;
    goto ok_cleanup;
  }
  if (block_device_read(chunked_4096, scratch_lba, scratch_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_BUILD_FAILED;
    goto ok_cleanup;
  }
  if (vp_recovery_block_zero(scratch_block, sizeof(scratch_block))) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_NOT_NEEDED;
    goto ok_cleanup;
  }
  if (volume_provider_rekey_checkpoint_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint) != 0 ||
      volume_provider_rekey_stage_manifest_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &manifest) != 0 ||
      !vp_recovery_scratch_integrity_valid(scratch_block, &manifest) ||
      checkpoint.flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED ||
      checkpoint.blocks_completed != checkpoint.blocks_total ||
      checkpoint.blocks_total != capyfs_blocks ||
      checkpoint.raw_block_count != chunked_4096->block_count ||
      checkpoint.capyfs_block_count != capyfs_blocks ||
      checkpoint.scratch_first_lba != scratch_lba ||
      (checkpoint.phase_flags &
       (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) !=
          (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
           VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
           VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN) ||
      manifest.scratch_lba != scratch_lba ||
      manifest.raw_block_count != chunked_4096->block_count ||
      manifest.capyfs_block_count != capyfs_blocks ||
      manifest.blocks_total != capyfs_blocks) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_BUILD_FAILED;
    goto ok_cleanup;
  }
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));
  if (block_device_write(chunked_4096, scratch_lba, scratch_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_WRITE_FAILED;
    goto ok_cleanup;
  }
  if (block_device_read(chunked_4096, scratch_lba, scratch_block) != 0 ||
      !vp_recovery_block_zero(scratch_block, sizeof(scratch_block))) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_VERIFY_FAILED;
    goto ok_cleanup;
  }
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;
  out->blocks_to_reencrypt = capyfs_blocks;
  out->blocks_completed = capyfs_blocks;
  out->blocks_remaining = 0u;
  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_DONE;

ok_cleanup:
  if (opened_crypt) crypt_free(opened_crypt);
  vp_recovery_wipe(&checkpoint, sizeof(checkpoint));
  vp_recovery_wipe(&manifest, sizeof(manifest));
  vp_recovery_wipe(header_block, sizeof(header_block));
  vp_recovery_wipe(super, sizeof(super));
  vp_recovery_wipe(scratch_block, sizeof(scratch_block));
  return 0;
}
