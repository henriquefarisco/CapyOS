#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "security/crypt.h"
#include "security/csprng.h"
#include "security/volume_header.h"

#define VP_REKEY_EXEC_BLOCK_SIZE 4096u
#define VP_REKEY_STAGE_SALT_LEN 16u

static void vp_rekey_exec_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void vp_rekey_put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t vp_rekey_get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

static int vp_rekey_stage_manifest_valid(
    const struct volume_provider_rekey_stage_manifest *manifest) {
  uint32_t source_blocks = 0;
  uint32_t target_blocks = 0;

  if (!manifest) {
    return 0;
  }
  if (manifest->magic0 != VOLUME_PROVIDER_REKEY_STAGE_MAGIC0 ||
      manifest->magic1 != VOLUME_PROVIDER_REKEY_STAGE_MAGIC1 ||
      manifest->version != VOLUME_PROVIDER_REKEY_STAGE_VERSION ||
      manifest->flags != 0u ||
      manifest->checkpoint_offset !=
          VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET ||
      manifest->checkpoint_size != VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE ||
      manifest->staged_header_offset !=
          VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET ||
      manifest->staged_header_size !=
          VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE ||
      manifest->manifest_offset !=
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET ||
      manifest->manifest_size != VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE ||
      manifest->raw_block_count == 0u ||
      manifest->capyfs_block_count == 0u ||
      manifest->blocks_total != manifest->capyfs_block_count ||
      manifest->source_first_lba > manifest->source_last_lba ||
      manifest->target_first_lba > manifest->target_last_lba ||
      manifest->scratch_lba <= manifest->target_last_lba ||
      manifest->raw_block_count <= manifest->scratch_lba) {
    return 0;
  }
  source_blocks = manifest->source_last_lba - manifest->source_first_lba + 1u;
  target_blocks = manifest->target_last_lba - manifest->target_first_lba + 1u;
  return manifest->blocks_total == source_blocks &&
         manifest->blocks_total == target_blocks;
}

static int vp_rekey_stage_manifest_same(
    const struct volume_provider_rekey_stage_manifest *a,
    const struct volume_provider_rekey_stage_manifest *b) {
  if (!a || !b) {
    return 0;
  }
  return a->magic0 == b->magic0 && a->magic1 == b->magic1 &&
         a->version == b->version && a->flags == b->flags &&
         a->checkpoint_offset == b->checkpoint_offset &&
         a->checkpoint_size == b->checkpoint_size &&
         a->staged_header_offset == b->staged_header_offset &&
         a->staged_header_size == b->staged_header_size &&
         a->manifest_offset == b->manifest_offset &&
         a->manifest_size == b->manifest_size &&
         a->scratch_lba == b->scratch_lba &&
         a->raw_block_count == b->raw_block_count &&
         a->capyfs_block_count == b->capyfs_block_count &&
         a->source_first_lba == b->source_first_lba &&
         a->source_last_lba == b->source_last_lba &&
         a->target_first_lba == b->target_first_lba &&
         a->target_last_lba == b->target_last_lba &&
         a->blocks_total == b->blocks_total &&
         a->checkpoint_crc32 == b->checkpoint_crc32 &&
         a->staged_header_crc32 == b->staged_header_crc32;
}

int volume_provider_rekey_stage_manifest_serialize(
    const struct volume_provider_rekey_stage_manifest *manifest,
    uint8_t *out,
    size_t out_len) {
  if (!manifest || !out ||
      out_len < VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE) {
    return -1;
  }
  if (!vp_rekey_stage_manifest_valid(manifest)) {
    return -1;
  }

  vp_rekey_exec_wipe(out, out_len);
  vp_rekey_put_u32_le(out + 0, manifest->magic0);
  vp_rekey_put_u32_le(out + 4, manifest->magic1);
  vp_rekey_put_u32_le(out + 8, manifest->version);
  vp_rekey_put_u32_le(out + 12, manifest->flags);
  vp_rekey_put_u32_le(out + 16, manifest->checkpoint_offset);
  vp_rekey_put_u32_le(out + 20, manifest->checkpoint_size);
  vp_rekey_put_u32_le(out + 24, manifest->staged_header_offset);
  vp_rekey_put_u32_le(out + 28, manifest->staged_header_size);
  vp_rekey_put_u32_le(out + 32, manifest->manifest_offset);
  vp_rekey_put_u32_le(out + 36, manifest->manifest_size);
  vp_rekey_put_u32_le(out + 40, manifest->scratch_lba);
  vp_rekey_put_u32_le(out + 44, manifest->raw_block_count);
  vp_rekey_put_u32_le(out + 48, manifest->capyfs_block_count);
  vp_rekey_put_u32_le(out + 52, manifest->source_first_lba);
  vp_rekey_put_u32_le(out + 56, manifest->source_last_lba);
  vp_rekey_put_u32_le(out + 60, manifest->target_first_lba);
  vp_rekey_put_u32_le(out + 64, manifest->target_last_lba);
  vp_rekey_put_u32_le(out + 68, manifest->blocks_total);
  vp_rekey_put_u32_le(out + 72, manifest->checkpoint_crc32);
  vp_rekey_put_u32_le(out + 76, manifest->staged_header_crc32);
  vp_rekey_put_u32_le(
      out + 124,
      capyos_volume_header_crc32(out,
                                 VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE -
                                     4u));
  return 0;
}

int volume_provider_rekey_stage_manifest_parse(
    const uint8_t *buf,
    size_t len,
    struct volume_provider_rekey_stage_manifest *out) {
  uint32_t stored_crc = 0;
  uint32_t calc_crc = 0;

  if (out) {
    vp_rekey_exec_wipe(out, sizeof(*out));
  }
  if (!buf || !out || len < VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE) {
    return -1;
  }

  stored_crc = vp_rekey_get_u32_le(buf + 124);
  calc_crc = capyos_volume_header_crc32(
      buf, VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE - 4u);
  if (stored_crc != calc_crc) {
    return -1;
  }
  for (size_t i = 80u; i < 124u; ++i) {
    if (buf[i] != 0u) {
      return -1;
    }
  }

  out->magic0 = vp_rekey_get_u32_le(buf + 0);
  out->magic1 = vp_rekey_get_u32_le(buf + 4);
  out->version = vp_rekey_get_u32_le(buf + 8);
  out->flags = vp_rekey_get_u32_le(buf + 12);
  out->checkpoint_offset = vp_rekey_get_u32_le(buf + 16);
  out->checkpoint_size = vp_rekey_get_u32_le(buf + 20);
  out->staged_header_offset = vp_rekey_get_u32_le(buf + 24);
  out->staged_header_size = vp_rekey_get_u32_le(buf + 28);
  out->manifest_offset = vp_rekey_get_u32_le(buf + 32);
  out->manifest_size = vp_rekey_get_u32_le(buf + 36);
  out->scratch_lba = vp_rekey_get_u32_le(buf + 40);
  out->raw_block_count = vp_rekey_get_u32_le(buf + 44);
  out->capyfs_block_count = vp_rekey_get_u32_le(buf + 48);
  out->source_first_lba = vp_rekey_get_u32_le(buf + 52);
  out->source_last_lba = vp_rekey_get_u32_le(buf + 56);
  out->target_first_lba = vp_rekey_get_u32_le(buf + 60);
  out->target_last_lba = vp_rekey_get_u32_le(buf + 64);
  out->blocks_total = vp_rekey_get_u32_le(buf + 68);
  out->checkpoint_crc32 = vp_rekey_get_u32_le(buf + 72);
  out->staged_header_crc32 = vp_rekey_get_u32_le(buf + 76);
  out->manifest_crc32 = stored_crc;
  if (!vp_rekey_stage_manifest_valid(out)) {
    vp_rekey_exec_wipe(out, sizeof(*out));
    return -1;
  }
  return 0;
}

int volume_provider_rekey_execute_checkpoint(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_plan plan;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_checkpoint parsed;
  uint8_t checkpoint_block[VP_REKEY_EXEC_BLOCK_SIZE];

  if (out) {
    vp_rekey_exec_wipe(out, sizeof(*out));
  }
  if (!out) {
    return -1;
  }
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE) {
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
  vp_rekey_exec_wipe(&parsed, sizeof(parsed));
  vp_rekey_exec_wipe(checkpoint_block, sizeof(checkpoint_block));

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_rekey_exec_wipe(out, sizeof(*out));
    goto fail;
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
    goto ok;
  }
  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    goto ok;
  }
  if (plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    goto ok;
  }

  out->next_source_lba = plan.source_last_lba;
  out->next_target_lba = plan.target_last_lba;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH;

  if (volume_provider_rekey_checkpoint_init(&plan, 0u, &checkpoint) != 0 ||
      volume_provider_rekey_checkpoint_serialize(&checkpoint, checkpoint_block,
                                                 sizeof(checkpoint_block)) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITE_FAILED;
    goto ok;
  }
  if (block_device_write(chunked_4096, plan.scratch_first_lba,
                         checkpoint_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITE_FAILED;
    goto ok;
  }

  vp_rekey_exec_wipe(checkpoint_block, sizeof(checkpoint_block));
  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        checkpoint_block) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          checkpoint_block, sizeof(checkpoint_block), &parsed) != 0 ||
      !vp_rekey_checkpoint_same(&checkpoint, &parsed)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_VERIFY_FAILED;
    goto ok;
  }

  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITTEN;

ok:
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&parsed, sizeof(parsed));
  vp_rekey_exec_wipe(checkpoint_block, sizeof(checkpoint_block));
  return 0;

fail:
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&parsed, sizeof(parsed));
  vp_rekey_exec_wipe(checkpoint_block, sizeof(checkpoint_block));
  return -1;
}

int volume_provider_rekey_execute_stage_header(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_plan plan;
  struct volume_provider_rekey_checkpoint checkpoint;
  struct volume_provider_rekey_checkpoint parsed_checkpoint;
  struct volume_provider_rekey_stage_manifest manifest;
  struct volume_provider_rekey_stage_manifest parsed_manifest;
  struct capyos_volume_header header;
  struct capyos_volume_header parsed_header;
  uint8_t scratch_block[VP_REKEY_EXEC_BLOCK_SIZE];
  uint8_t salt[VP_REKEY_STAGE_SALT_LEN];
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  uint8_t tag[CAPYOS_VOLUME_HEADER_CHECK_TAG_SIZE];

  if (out) {
    vp_rekey_exec_wipe(out, sizeof(*out));
  }
  if (!out) {
    return -1;
  }
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE) {
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
  vp_rekey_exec_wipe(&parsed_checkpoint, sizeof(parsed_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&parsed_manifest, sizeof(parsed_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&parsed_header, sizeof(parsed_header));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(salt, sizeof(salt));
  vp_rekey_exec_wipe(key1, sizeof(key1));
  vp_rekey_exec_wipe(key2, sizeof(key2));
  vp_rekey_exec_wipe(tag, sizeof(tag));

  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_rekey_exec_wipe(out, sizeof(*out));
    goto fail;
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
    goto ok;
  }
  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    goto ok;
  }
  if (plan.scratch_first_lba >= chunked_4096->block_count) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    goto ok;
  }

  out->next_source_lba = plan.source_last_lba;
  out->next_target_lba = plan.target_last_lba;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER;

  if (volume_provider_rekey_checkpoint_init(&plan, 0u, &checkpoint) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED;
    goto ok;
  }
  checkpoint.phase_flags |= VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER;
  if (volume_provider_rekey_checkpoint_serialize(
          &checkpoint,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED;
    goto ok;
  }

  csprng_get_bytes(salt, sizeof(salt));
  if (capyos_volume_header_init(&header, CAPYOS_VOLUME_KDF_ALGO_ARGON2ID,
                                CRYPT_VOLUME_ARGON2ID_T_COST,
                                CRYPT_VOLUME_ARGON2ID_M_COST, salt,
                                sizeof(salt),
                                CAPYOS_VOLUME_HEADER_DEFAULT_DATA_OFFSET_LBA,
                                CAPYOS_VOLUME_HEADER_DEFAULT_RESERVED_LBA_COUNT,
                                0u, "CapyOS-0.8.0-alpha.232") !=
      CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED;
    goto ok;
  }
  if (crypt_derive_xts_keys_argon2id(password, header.kdf_salt,
                                     header.kdf_salt_len, header.kdf_t_cost,
                                     header.kdf_m_cost, key1, key2) != 0 ||
      capyos_volume_header_compute_check_tag(&header, key1, key2, tag) !=
          CAPYOS_VOLUME_HEADER_OK ||
      capyos_volume_header_finalize_crc(&header) != CAPYOS_VOLUME_HEADER_OK ||
      capyos_volume_header_serialize(
          &header,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET) !=
          CAPYOS_VOLUME_HEADER_OK) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED;
    goto ok;
  }

  manifest.magic0 = VOLUME_PROVIDER_REKEY_STAGE_MAGIC0;
  manifest.magic1 = VOLUME_PROVIDER_REKEY_STAGE_MAGIC1;
  manifest.version = VOLUME_PROVIDER_REKEY_STAGE_VERSION;
  manifest.checkpoint_offset = VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET;
  manifest.checkpoint_size = VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE;
  manifest.staged_header_offset = VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET;
  manifest.staged_header_size = VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE;
  manifest.manifest_offset = VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET;
  manifest.manifest_size = VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE;
  manifest.scratch_lba = plan.scratch_first_lba;
  manifest.raw_block_count = plan.raw_block_count;
  manifest.capyfs_block_count = plan.capyfs_block_count;
  manifest.source_first_lba = plan.source_first_lba;
  manifest.source_last_lba = plan.source_last_lba;
  manifest.target_first_lba = plan.target_first_lba;
  manifest.target_last_lba = plan.target_last_lba;
  manifest.blocks_total = plan.blocks_to_reencrypt;
  manifest.checkpoint_crc32 = capyos_volume_header_crc32(
      scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
      VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE);
  manifest.staged_header_crc32 = capyos_volume_header_crc32(
      scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
      VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE);
  if (volume_provider_rekey_stage_manifest_serialize(
          &manifest,
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED;
    goto ok;
  }

  if (block_device_write(chunked_4096, plan.scratch_first_lba,
                         scratch_block) != 0) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITE_FAILED;
    goto ok;
  }

  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  if (block_device_read(chunked_4096, plan.scratch_first_lba,
                        scratch_block) != 0 ||
      volume_provider_rekey_checkpoint_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
          VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE, &parsed_checkpoint) != 0 ||
      !vp_rekey_checkpoint_same(&checkpoint, &parsed_checkpoint) ||
      capyos_volume_header_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
          &parsed_header) != CAPYOS_VOLUME_HEADER_OK ||
      capyos_volume_header_verify_check_tag(&parsed_header, key1, key2) !=
          CAPYOS_VOLUME_HEADER_OK ||
      volume_provider_rekey_stage_manifest_parse(
          scratch_block + VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET,
          VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE, &parsed_manifest) != 0 ||
      !vp_rekey_stage_manifest_same(&manifest, &parsed_manifest) ||
      parsed_manifest.checkpoint_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET,
              VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) ||
      parsed_manifest.staged_header_crc32 !=
          capyos_volume_header_crc32(
              scratch_block + VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET,
              VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE)) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_VERIFY_FAILED;
    goto ok;
  }

  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN;

ok:
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&parsed_checkpoint, sizeof(parsed_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&parsed_manifest, sizeof(parsed_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&parsed_header, sizeof(parsed_header));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(salt, sizeof(salt));
  vp_rekey_exec_wipe(key1, sizeof(key1));
  vp_rekey_exec_wipe(key2, sizeof(key2));
  vp_rekey_exec_wipe(tag, sizeof(tag));
  return 0;

fail:
  vp_rekey_exec_wipe(&plan, sizeof(plan));
  vp_rekey_exec_wipe(&checkpoint, sizeof(checkpoint));
  vp_rekey_exec_wipe(&parsed_checkpoint, sizeof(parsed_checkpoint));
  vp_rekey_exec_wipe(&manifest, sizeof(manifest));
  vp_rekey_exec_wipe(&parsed_manifest, sizeof(parsed_manifest));
  vp_rekey_exec_wipe(&header, sizeof(header));
  vp_rekey_exec_wipe(&parsed_header, sizeof(parsed_header));
  vp_rekey_exec_wipe(scratch_block, sizeof(scratch_block));
  vp_rekey_exec_wipe(salt, sizeof(salt));
  vp_rekey_exec_wipe(key1, sizeof(key1));
  vp_rekey_exec_wipe(key2, sizeof(key2));
  vp_rekey_exec_wipe(tag, sizeof(tag));
  return -1;
}
