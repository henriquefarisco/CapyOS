/*
 * CapyOS volume provider rekey copy-step executor (alpha.229).
 *
 * Owns `volume_provider_rekey_execute_copy_step`: the write-enabled
 * reverse copy + re-encrypt of one block from the legacy domain
 * (PBKDF2 over the full device) to the header-managed domain
 * (Argon2id-bound keys over the offset-shifted FS), with explicit
 * scratch-checkpoint update and read-back verification after every
 * step. Sibling translation units in this directory provide the
 * other phases of the write-enabled rekey state machine:
 *
 *   - `volume_provider_rekey_execute.c`: write checkpoint to scratch
 *     and stage the destination header.
 *   - `volume_provider_rekey_commit.c`: swap LBA0 atomically.
 *   - `volume_provider_rekey_recovery.c`: rollback, abort, cleanup.
 *   - `volume_provider_rekey_orchestrator.c`: one-step driver.
 *
 * The wire format (checkpoint + stage manifest) is defined alongside
 * the read-only rekey orchestration in `volume_provider_rekey.c`
 * (preflight + plan + dry-run execute + checkpoint init/serialize/
 * parse + stage manifest is in `volume_provider_rekey_execute.c`).
 *
 * Wipe hygiene: every exit path zeros local key material, plaintext
 * blocks, the offset-wrapper view and the checkpoint/manifest
 * scratch via the volatile-safe `vp_rekey_exec_wipe` helper. This
 * TU keeps its own static copies of `vp_rekey_exec_wipe`,
 * `vp_rekey_block_same`, `vp_rekey_checkpoint_same` and the offset
 * wrapper plumbing (`vp_rekey_offset_view` + read/write callbacks +
 * `vp_rekey_offset_ops`) to preserve the established per-TU
 * "no link-time coupling" pattern.
 */

#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "security/crypt.h"
#include "security/volume_header.h"

#define VP_REKEY_EXEC_BLOCK_SIZE 4096u

struct vp_rekey_offset_view {
  struct block_device *lower;
  uint32_t start_lba;
  uint32_t block_count;
};

static void vp_rekey_exec_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static int vp_rekey_offset_read(void *ctx, uint32_t block_no, void *buffer) {
  struct vp_rekey_offset_view *view = (struct vp_rekey_offset_view *)ctx;
  if (!view || !view->lower || !buffer || block_no >= view->block_count) {
    return -1;
  }
  return block_device_read(view->lower, view->start_lba + block_no, buffer);
}

static int vp_rekey_offset_write(void *ctx, uint32_t block_no,
                                 const void *buffer) {
  struct vp_rekey_offset_view *view = (struct vp_rekey_offset_view *)ctx;
  if (!view || !view->lower || !buffer || block_no >= view->block_count) {
    return -1;
  }
  return block_device_write(view->lower, view->start_lba + block_no, buffer);
}

static const struct block_device_ops vp_rekey_offset_ops = {
    .read_block = vp_rekey_offset_read,
    .write_block = vp_rekey_offset_write,
};

static int vp_rekey_block_same(const uint8_t *a, const uint8_t *b,
                               size_t len) {
  uint8_t diff = 0u;
  if (!a || !b) {
    return 0;
  }
  for (size_t i = 0u; i < len; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0u;
}

static int vp_rekey_checkpoint_same(
    const struct volume_provider_rekey_checkpoint *a,
    const struct volume_provider_rekey_checkpoint *b) {
  if (!a || !b) {
    return 0;
  }
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

int volume_provider_rekey_execute_copy_step(
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
  struct vp_rekey_offset_view target_view_ctx;
  struct block_device target_view;
  struct block_device *legacy_crypt = NULL;
  struct block_device *target_crypt = NULL;
  uint8_t scratch_block[VP_REKEY_EXEC_BLOCK_SIZE];
  uint8_t plain[VP_REKEY_EXEC_BLOCK_SIZE];
  uint8_t legacy_key1[CRYPT_KEY_SIZE];
  uint8_t legacy_key2[CRYPT_KEY_SIZE];
  uint8_t target_key1[CRYPT_KEY_SIZE];
  uint8_t target_key2[CRYPT_KEY_SIZE];
  uint32_t logical_lba = 0u;
  uint32_t completed = 0u;

  if (out) {
    vp_rekey_exec_wipe(out, sizeof(*out));
  }
  if (!out) {
    return -1;
  }
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  if (!chunked_4096 || !password ||
      chunked_4096->block_size != VP_REKEY_EXEC_BLOCK_SIZE) {
    return -1;
  }

  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_rekey_exec_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_rekey_exec_wipe(&target_view, sizeof(target_view));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(plain, sizeof(plain));
  vp_rekey_exec_wipe(legacy_key1, sizeof(legacy_key1));
  vp_rekey_exec_wipe(legacy_key2, sizeof(legacy_key2));
  vp_rekey_exec_wipe(target_key1, sizeof(target_key1));
  vp_rekey_exec_wipe(target_key2, sizeof(target_key2));

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_rekey_exec_wipe(out, sizeof(*out));
    goto fail_copy;
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
    goto ok_copy;
  }
  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    if (plan.scratch_first_lba >= chunked_4096->block_count) {
      out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    }
    goto ok_copy;
  }

  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE;

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
      manifest.checkpoint_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) ||
      manifest.staged_header_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED;
    goto ok_copy;
  }
  if (checkpoint.scratch_first_lba != plan.scratch_first_lba ||
      checkpoint.raw_block_count != plan.raw_block_count ||
      checkpoint.capyfs_block_count != plan.capyfs_block_count ||
      checkpoint.source_first_lba != plan.source_first_lba ||
      checkpoint.source_last_lba != plan.source_last_lba ||
      checkpoint.target_first_lba != plan.target_first_lba ||
      checkpoint.target_last_lba != plan.target_last_lba ||
      checkpoint.blocks_total != plan.blocks_to_reencrypt ||
      manifest.scratch_lba != plan.scratch_first_lba ||
      manifest.raw_block_count != plan.raw_block_count ||
      manifest.capyfs_block_count != plan.capyfs_block_count ||
      manifest.source_first_lba != plan.source_first_lba ||
      manifest.source_last_lba != plan.source_last_lba ||
      manifest.target_first_lba != plan.target_first_lba ||
      manifest.target_last_lba != plan.target_last_lba ||
      manifest.blocks_total != plan.blocks_to_reencrypt ||
      (checkpoint.phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) ==
          0u) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED;
    goto ok_copy;
  }
  if (crypt_derive_xts_keys_argon2id(password, header.kdf_salt,
                                     header.kdf_salt_len, header.kdf_t_cost,
                                     header.kdf_m_cost, target_key1,
                                     target_key2) != 0 ||
      capyos_volume_header_verify_check_tag(&header, target_key1,
                                            target_key2) !=
          CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED;
    goto ok_copy;
  }
  crypt_derive_xts_keys(password, legacy_salt, legacy_salt_len, legacy_iter,
                        legacy_key1, legacy_key2);

  if (checkpoint.blocks_completed >= checkpoint.blocks_total) {
    out->blocks_completed = checkpoint.blocks_completed;
    out->blocks_remaining = 0u;
    out->next_source_lba = checkpoint.next_source_lba;
    out->next_target_lba = checkpoint.next_target_lba;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE;
    goto ok_copy;
  }
  if (checkpoint.next_source_lba < checkpoint.source_first_lba ||
      checkpoint.next_target_lba < checkpoint.target_first_lba ||
      checkpoint.next_source_lba > checkpoint.source_last_lba ||
      checkpoint.next_target_lba > checkpoint.target_last_lba) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED;
    goto ok_copy;
  }

  logical_lba = checkpoint.next_source_lba - checkpoint.source_first_lba;
  target_view_ctx.lower = chunked_4096;
  target_view_ctx.start_lba = checkpoint.target_first_lba;
  target_view_ctx.block_count = checkpoint.blocks_total;
  target_view.name = "rekey-target-offset";
  target_view.block_size = chunked_4096->block_size;
  target_view.block_count = checkpoint.blocks_total;
  target_view.ctx = &target_view_ctx;
  target_view.ops = &vp_rekey_offset_ops;

  legacy_crypt = crypt_init(chunked_4096, legacy_key1, legacy_key2);
  target_crypt = crypt_init(&target_view, target_key1, target_key2);
  if (!legacy_crypt || !target_crypt) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED;
    goto ok_copy;
  }
  if (block_device_read(legacy_crypt, checkpoint.next_source_lba, plain) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_READ_FAILED;
    goto ok_copy;
  }
  if (block_device_write(target_crypt, logical_lba, plain) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_WRITE_FAILED;
    goto ok_copy;
  }
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  if (block_device_read(target_crypt, logical_lba, scratch_block) != 0 ||
      !vp_rekey_block_same(plain, scratch_block, sizeof(plain))) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_VERIFY_FAILED;
    goto ok_copy;
  }
  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        scratch_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED;
    goto ok_copy;
  }

  updated_checkpoint = checkpoint;
  completed = checkpoint.blocks_completed + 1u;
  updated_checkpoint.blocks_completed = completed;
  updated_checkpoint.flags = VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS;
  updated_checkpoint.phase_flags =
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE;
  if (completed == checkpoint.blocks_total) {
    updated_checkpoint.next_source_lba = checkpoint.source_first_lba;
    updated_checkpoint.next_target_lba = checkpoint.target_first_lba;
  } else {
    updated_checkpoint.next_source_lba =
        checkpoint.source_last_lba - completed;
    updated_checkpoint.next_target_lba =
        checkpoint.target_last_lba - completed;
  }
  if (volume_provider_rekey_checkpoint_serialize(
          &updated_checkpoint,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED;
    goto ok_copy;
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
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED;
    goto ok_copy;
  }
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        scratch_block) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &verified_checkpoint) != 0 ||
      !vp_rekey_checkpoint_same(&updated_checkpoint, &verified_checkpoint) ||
      volume_provider_rekey_stage_manifest_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &verified_manifest) != 0 ||
      verified_manifest.checkpoint_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) ||
      verified_manifest.staged_header_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED;
    goto ok_copy;
  }

  out->blocks_completed = completed;
  out->blocks_remaining = checkpoint.blocks_total - completed;
  out->next_source_lba = updated_checkpoint.next_source_lba;
  out->next_target_lba = updated_checkpoint.next_target_lba;
  out->status =
      completed == checkpoint.blocks_total
          ? VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE
          : VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE;

ok_copy:
  if (target_crypt) crypt_free(target_crypt);
  if (legacy_crypt) crypt_free(legacy_crypt);
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_rekey_exec_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_rekey_exec_wipe(&target_view, sizeof(target_view));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(plain, sizeof(plain));
  vp_rekey_exec_wipe(legacy_key1, sizeof(legacy_key1));
  vp_rekey_exec_wipe(legacy_key2, sizeof(legacy_key2));
  vp_rekey_exec_wipe(target_key1, sizeof(target_key1));
  vp_rekey_exec_wipe(target_key2, sizeof(target_key2));
  return 0;

fail_copy:
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&updated_checkpoint, sizeof(updated_checkpoint));
  vp_rekey_exec_wipe(&verified_checkpoint, sizeof(verified_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&verified_manifest, sizeof(verified_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&target_view_ctx, sizeof(target_view_ctx));
  vp_rekey_exec_wipe(&target_view, sizeof(target_view));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(plain, sizeof(plain));
  vp_rekey_exec_wipe(legacy_key1, sizeof(legacy_key1));
  vp_rekey_exec_wipe(legacy_key2, sizeof(legacy_key2));
  vp_rekey_exec_wipe(target_key1, sizeof(target_key1));
  vp_rekey_exec_wipe(target_key2, sizeof(target_key2));
  return -1;
}
