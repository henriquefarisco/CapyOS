#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "security/volume_header.h"

#define VP_REKEY_ORCH_BLOCK_SIZE 4096u
#define VP_REKEY_ORCH_ALLOWED_FLAGS \
  (VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE | \
   VOLUME_PROVIDER_REKEY_EXEC_FLAG_ORCHESTRATE_ABORT)

static void vp_orch_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static int vp_orch_block_zero(const uint8_t *block, size_t len) {
  uint8_t diff = 0u;
  if (!block) return 0;
  for (size_t i = 0u; i < len; ++i) {
    diff |= block[i];
  }
  return diff == 0u;
}

static int vp_orch_plan_matches_checkpoint(
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

static int vp_orch_plan_matches_manifest(
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

static int vp_orch_scratch_integrity_valid(
    const uint8_t *scratch,
    const struct volume_provider_rekey_stage_manifest *manifest) {
  if (!scratch || !manifest) return 0;
  return manifest->checkpoint_offset ==
             VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET &&
         manifest->checkpoint_size == VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE &&
         manifest->staged_header_offset ==
             VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET &&
         manifest->staged_header_size == VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE &&
         manifest->manifest_offset == VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET &&
         manifest->manifest_size == VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE &&
         manifest->checkpoint_crc32 ==
             capyos_volume_header_crc32(
                 scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
                 VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) &&
         manifest->staged_header_crc32 ==
             capyos_volume_header_crc32(
                 scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
                 VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE);
}

static void vp_orch_report_from_plan(
    struct volume_provider_rekey_execution_report *out,
    uint32_t flags,
    const struct volume_provider_rekey_plan *plan) {
  out->flags = flags;
  out->plan_status = plan->status;
  out->blocker_flags = plan->blocker_flags;
  out->blocks_to_reencrypt = plan->blocks_to_reencrypt;
  out->scratch_first_lba = plan->scratch_first_lba;
  out->estimated_read_ops = plan->estimated_read_ops;
  out->estimated_write_ops = plan->estimated_write_ops;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN;
}

static int vp_orch_stage(struct block_device *chunked_4096,
                         const char *password,
                         const uint8_t *legacy_salt,
                         size_t legacy_salt_len,
                         uint32_t legacy_iter,
                         uint32_t flags,
                         struct volume_provider_rekey_execution_report *out) {
  int rc = volume_provider_rekey_execute_stage_header(
      chunked_4096, password, legacy_salt, legacy_salt_len, legacy_iter,
      VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE, out);
  if (rc == 0) out->flags = flags;
  return rc;
}

static int vp_orch_copy(struct block_device *chunked_4096,
                        const char *password,
                        const uint8_t *legacy_salt,
                        size_t legacy_salt_len,
                        uint32_t legacy_iter,
                        uint32_t flags,
                        struct volume_provider_rekey_execution_report *out) {
  int rc = volume_provider_rekey_execute_copy_step(
      chunked_4096, password, legacy_salt, legacy_salt_len, legacy_iter,
      VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP, out);
  if (rc == 0) out->flags = flags;
  return rc;
}

static int vp_orch_commit(struct block_device *chunked_4096,
                          const char *password,
                          const uint8_t *legacy_salt,
                          size_t legacy_salt_len,
                          uint32_t legacy_iter,
                          uint32_t flags,
                          struct volume_provider_rekey_execution_report *out) {
  int rc = volume_provider_rekey_execute_commit_header(
      chunked_4096, password, legacy_salt, legacy_salt_len, legacy_iter,
      VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER, out);
  if (rc == 0) out->flags = flags;
  return rc;
}

static int vp_orch_rollback(struct block_device *chunked_4096,
                            const char *password,
                            const uint8_t *legacy_salt,
                            size_t legacy_salt_len,
                            uint32_t legacy_iter,
                            uint32_t flags,
                            struct volume_provider_rekey_execution_report *out) {
  int rc = volume_provider_rekey_execute_rollback_step(
      chunked_4096, password, legacy_salt, legacy_salt_len, legacy_iter,
      VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP, out);
  if (rc == 0) out->flags = flags;
  return rc;
}

static int vp_orch_cleanup(struct block_device *chunked_4096,
                           const char *password,
                           const uint8_t *legacy_salt,
                           size_t legacy_salt_len,
                           uint32_t legacy_iter,
                           uint32_t flags,
                           struct volume_provider_rekey_execution_report *out) {
  int rc = volume_provider_rekey_execute_cleanup_scratch(
      chunked_4096, password, legacy_salt, legacy_salt_len, legacy_iter,
      VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH, out);
  if (rc == 0) out->flags = flags;
  return rc;
}

int volume_provider_rekey_execute_orchestrated_step(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_plan plan;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_stage_manifest manifest;
  struct capyos_volume_header header;
  uint8_t lba0[VP_REKEY_ORCH_BLOCK_SIZE];
  uint8_t scratch[VP_REKEY_ORCH_BLOCK_SIZE];
  uint32_t abort_requested = 0u;
  int rc = 0;

  if (out) vp_orch_wipe(out, sizeof(*out));
  if (!out) return -1;
  if ((flags & VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE) == 0u ||
      (flags & ~VP_REKEY_ORCH_ALLOWED_FLAGS) != 0u) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  if (!chunked_4096 || !password ||
      chunked_4096->block_size != VP_REKEY_ORCH_BLOCK_SIZE) {
    return -1;
  }

  vp_orch_wipe(&plan, sizeof(plan));
  vp_orch_wipe(&checkpoint, sizeof(checkpoint));
  vp_orch_wipe(&manifest, sizeof(manifest));
  vp_orch_wipe(&header, sizeof(header));
  vp_orch_wipe(lba0, sizeof(lba0));
  vp_orch_wipe(scratch, sizeof(scratch));

  abort_requested =
      (flags & VOLUME_PROVIDER_REKEY_EXEC_FLAG_ORCHESTRATE_ABORT) != 0u;

  if (block_device_read(chunked_4096, 0u, lba0) != 0) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }
  if (capyos_volume_header_looks_valid(lba0)) {
    rc = vp_orch_cleanup(chunked_4096, password, legacy_salt, legacy_salt_len,
                         legacy_iter, flags, out);
    goto done;
  }

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_orch_wipe(out, sizeof(*out));
    goto fail;
  }

  vp_orch_report_from_plan(out, flags, &plan);
  if (plan.status == VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED;
    goto ok;
  }
  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    if (plan.scratch_first_lba >= chunked_4096->block_count) {
      out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    }
    goto ok;
  }

  if (block_device_read(chunked_4096, plan.scratch_first_lba, scratch) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }
  if (vp_orch_block_zero(scratch, sizeof(scratch))) {
    if (abort_requested) {
      out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_NOT_NEEDED;
      goto ok;
    }
    rc = vp_orch_stage(chunked_4096, password, legacy_salt, legacy_salt_len,
                       legacy_iter, flags, out);
    goto done;
  }

  if (volume_provider_rekey_checkpoint_parse(
          scratch + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &checkpoint) != 0 ||
      volume_provider_rekey_stage_manifest_parse(
          scratch + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &manifest) != 0 ||
      capyos_volume_header_parse(
          scratch + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
          &header) != CAPYOS_VOLUME_HEADER_OK ||
      !vp_orch_scratch_integrity_valid(scratch, &manifest) ||
      !vp_orch_plan_matches_checkpoint(&plan, &checkpoint) ||
      !vp_orch_plan_matches_manifest(&plan, &manifest) ||
      header.kdf_algo_id != CAPYOS_VOLUME_KDF_ALGO_ARGON2ID ||
      header.data_offset_lba != plan.target_first_lba ||
      (checkpoint.phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER) ==
          0u) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }

  if (checkpoint.blocks_completed > checkpoint.blocks_total) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }
  out->phase_flags = checkpoint.phase_flags;
  out->blocks_completed = checkpoint.blocks_completed;
  out->blocks_remaining = checkpoint.blocks_total - checkpoint.blocks_completed;
  out->next_source_lba = checkpoint.next_source_lba;
  out->next_target_lba = checkpoint.next_target_lba;

  if (abort_requested) {
    if (checkpoint.flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS ||
        (checkpoint.phase_flags &
         (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
          VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u) {
      out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
      goto ok;
    }
    rc = vp_orch_rollback(chunked_4096, password, legacy_salt, legacy_salt_len,
                          legacy_iter, flags, out);
    goto done;
  }

  if (checkpoint.flags == VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED ||
      (checkpoint.phase_flags &
       (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }
  if (checkpoint.flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED;
    goto ok;
  }
  if (checkpoint.blocks_completed < checkpoint.blocks_total) {
    rc = vp_orch_copy(chunked_4096, password, legacy_salt, legacy_salt_len,
                      legacy_iter, flags, out);
    goto done;
  }
  rc = vp_orch_commit(chunked_4096, password, legacy_salt, legacy_salt_len,
                      legacy_iter, flags, out);
  goto done;

ok:
  rc = 0;

done:
  vp_orch_wipe(&plan, sizeof(plan));
  vp_orch_wipe(&checkpoint, sizeof(checkpoint));
  vp_orch_wipe(&manifest, sizeof(manifest));
  vp_orch_wipe(&header, sizeof(header));
  vp_orch_wipe(lba0, sizeof(lba0));
  vp_orch_wipe(scratch, sizeof(scratch));
  return rc;

fail:
  vp_orch_wipe(&plan, sizeof(plan));
  vp_orch_wipe(&checkpoint, sizeof(checkpoint));
  vp_orch_wipe(&manifest, sizeof(manifest));
  vp_orch_wipe(&header, sizeof(header));
  vp_orch_wipe(lba0, sizeof(lba0));
  vp_orch_wipe(scratch, sizeof(scratch));
  return -1;
}
