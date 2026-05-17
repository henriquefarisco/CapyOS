/*
 * CapyOS volume provider rekey orchestration (alpha.222-alpha.226).
 *
 * Contract and threat model live in `include/security/volume_provider.h`.
 * This translation unit owns the read-only rekey orchestration surface:
 *
 *   - `volume_provider_rekey_preflight`: probes legacy/header-managed
 *     state and reports actions/blockers without writing to disk.
 *   - `volume_provider_rekey_plan`: turns the preflight into a
 *     read-only transactional plan with scratch geometry.
 *   - `volume_provider_rekey_execute` (dry-run dispatcher): validates
 *     the plan and surfaces phase flags without authorising writes.
 *   - `volume_provider_rekey_checkpoint_{init,serialize,parse}`:
 *     little-endian wire format + CRC32 + semantic validator for the
 *     persistent checkpoint consumed by the write-enabled executor.
 *
 * Write-enabled execute/commit/recovery steps live in their own
 * sibling translation units (`volume_provider_rekey_execute.c`,
 * `volume_provider_rekey_commit.c`, `volume_provider_rekey_recovery.c`,
 * `volume_provider_rekey_orchestrator.c`) and consume the same wire
 * format defined here.
 *
 * Wipe hygiene: every exit path zeros local key material via the
 * volatile-safe `vp_wipe` helper before returning, matching the rest
 * of the security primitives in the project. This TU keeps its own
 * static `vp_wipe` / `vp_get_u32_le` / `vp_put_u32_le` / `vp_crc32`
 * copies to preserve the per-TU "no link-time coupling" pattern
 * already used by `volume_provider.c` and `volume_header.c`.
 */

#include "security/volume_provider.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "fs/capyfs.h"
#include "security/crypt.h"
#include "security/volume_header.h"

/*
 * 4 KiB block size that the chunked wrapper produces. Hard-coded
 * here (matching the sibling TU in `volume_provider.c`) to avoid
 * coupling this module to the CAPYFS macro and to make it
 * self-evident in code review.
 */
#define VP_BLOCK_4K_SIZE 4096u

/*
 * Volatile-safe wipe. Mirrors the helper in `volume_provider.c` and
 * `volume_header.c` so this translation unit has no link-time
 * coupling to other security primitives beyond what's strictly
 * necessary.
 */
static void vp_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static uint32_t vp_get_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void vp_put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t vp_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  if (!data) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint32_t)data[i];
    for (int bit = 0; bit < 8; ++bit) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

static int vp_rekey_checkpoint_semantic_valid(
    const struct volume_provider_rekey_checkpoint *checkpoint) {
  uint32_t expected_source = 0;
  uint32_t expected_target = 0;
  uint32_t source_blocks = 0;
  uint32_t target_blocks = 0;
  uint32_t allowed_phases = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
      VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;

  if (!checkpoint) {
    return 0;
  }
  if (checkpoint->magic0 != VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC0 ||
      checkpoint->magic1 != VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC1 ||
      checkpoint->version != VOLUME_PROVIDER_REKEY_CHECKPOINT_VERSION ||
      checkpoint->plan_status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      checkpoint->source_layout != VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE ||
      checkpoint->target_layout != VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED ||
      checkpoint->blocker_flags != 0u || checkpoint->blocks_total == 0u ||
      checkpoint->blocks_completed > checkpoint->blocks_total) {
    return 0;
  }
  if (checkpoint->flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS &&
      checkpoint->flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED) {
    return 0;
  }
  if (checkpoint->blocks_completed < checkpoint->blocks_total &&
      checkpoint->flags != VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS) {
    return 0;
  }
  if ((checkpoint->phase_flags &
       VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN) == 0u ||
      (checkpoint->phase_flags &
       VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH) == 0u ||
      (checkpoint->phase_flags & ~allowed_phases) != 0u) {
    return 0;
  }
  if (checkpoint->blocks_completed == 0u &&
      (checkpoint->phase_flags &
       (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u) {
    return 0;
  }
  if (checkpoint->blocks_completed > 0u &&
      checkpoint->blocks_completed < checkpoint->blocks_total &&
      ((checkpoint->phase_flags & VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE)
           == 0u ||
       (checkpoint->phase_flags &
        (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
         VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u)) {
    return 0;
  }
  if (checkpoint->blocks_completed == checkpoint->blocks_total) {
    uint32_t terminal_phases = VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;
    if (checkpoint->flags == VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED &&
        (checkpoint->phase_flags & terminal_phases) != terminal_phases) return 0;
    if (checkpoint->flags == VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS &&
        ((checkpoint->phase_flags &
          VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE) == 0u ||
         (checkpoint->phase_flags & (VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
                                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN)) != 0u)) {
      return 0;
    }
  }
  if (checkpoint->source_first_lba > checkpoint->source_last_lba ||
      checkpoint->target_first_lba > checkpoint->target_last_lba ||
      checkpoint->scratch_first_lba <= checkpoint->target_last_lba ||
      checkpoint->raw_block_count <= checkpoint->scratch_first_lba ||
      checkpoint->blocks_total != checkpoint->capyfs_block_count) {
    return 0;
  }
  source_blocks = checkpoint->source_last_lba - checkpoint->source_first_lba +
                  1u;
  target_blocks = checkpoint->target_last_lba - checkpoint->target_first_lba +
                  1u;
  if (checkpoint->blocks_total != source_blocks ||
      checkpoint->blocks_total != target_blocks) {
    return 0;
  }
  if (checkpoint->blocks_completed == checkpoint->blocks_total) {
    expected_source = checkpoint->source_first_lba;
    expected_target = checkpoint->target_first_lba;
  } else {
    expected_source = checkpoint->source_last_lba -
                      checkpoint->blocks_completed;
    expected_target = checkpoint->target_last_lba -
                      checkpoint->blocks_completed;
  }
  return checkpoint->next_source_lba == expected_source &&
         checkpoint->next_target_lba == expected_target;
}

static int vp_rekey_checkpoint_plan_valid(
    const struct volume_provider_rekey_plan *plan) {
  uint32_t source_blocks = 0;
  uint32_t target_blocks = 0;

  if (!plan) {
    return 0;
  }
  if (plan->status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY ||
      plan->source_layout != VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE ||
      plan->target_layout != VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED ||
      plan->blocker_flags != 0u ||
      plan->copy_direction != VOLUME_PROVIDER_REKEY_COPY_DIRECTION_REVERSE ||
      plan->raw_block_count == 0u || plan->capyfs_block_count == 0u ||
      plan->blocks_to_reencrypt != plan->capyfs_block_count ||
      plan->source_first_lba > plan->source_last_lba ||
      plan->target_first_lba > plan->target_last_lba ||
      plan->scratch_first_lba <= plan->target_last_lba ||
      plan->raw_block_count <= plan->scratch_first_lba) {
    return 0;
  }
  source_blocks = plan->source_last_lba - plan->source_first_lba + 1u;
  target_blocks = plan->target_last_lba - plan->target_first_lba + 1u;
  if (plan->blocks_to_reencrypt != source_blocks ||
      plan->blocks_to_reencrypt != target_blocks) {
    return 0;
  }
  return 1;
}

static int vp_capyfs_plain_super_valid(const uint8_t *super,
                                       uint32_t visible_blocks) {
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

  if (!super) {
    return 0;
  }
  block_count = vp_get_u32_le(super + 12);
  inode_count = vp_get_u32_le(super + 16);
  bmap_start = vp_get_u32_le(super + 20);
  imap_start = vp_get_u32_le(super + 24);
  inode_start = vp_get_u32_le(super + 28);
  data_start = vp_get_u32_le(super + 32);

  if (vp_get_u32_le(super + 0) != CAPYFS_MAGIC ||
      vp_get_u32_le(super + 4) != CAPYFS_VERSION ||
      vp_get_u32_le(super + 8) != CAPYFS_BLOCK_SIZE || block_count == 0u ||
      inode_count == 0u || block_count > visible_blocks) {
    return 0;
  }
  block_bitmap_blocks = (block_count + bits_per_block - 1u) / bits_per_block;
  inode_bitmap_blocks = (inode_count + bits_per_block - 1u) / bits_per_block;
  inode_table_blocks =
      (inode_count * (uint32_t)sizeof(struct capy_inode_disk) +
       CAPYFS_BLOCK_SIZE - 1u) /
      CAPYFS_BLOCK_SIZE;
  if (bmap_start != 1u) {
    return 0;
  }
  if (imap_start != bmap_start + block_bitmap_blocks) {
    return 0;
  }
  if (inode_start != imap_start + inode_bitmap_blocks) {
    return 0;
  }
  if (data_start != inode_start + inode_table_blocks) {
    return 0;
  }
  return data_start < block_count;
}

int volume_provider_rekey_preflight(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    struct volume_provider_rekey_preflight *out) {
  if (out) {
    vp_wipe(out, sizeof(*out));
  }
  if (!chunked_4096 || !password || !out) {
    return -1;
  }
  if (chunked_4096->block_size != VP_BLOCK_4K_SIZE ||
      chunked_4096->block_count < 2u) {
    return -1;
  }

  uint8_t header_buf[VP_BLOCK_4K_SIZE];
  uint8_t plain_super[VP_BLOCK_4K_SIZE];
  struct block_device *legacy_crypt = NULL;
  struct capyos_volume_header hdr;
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  int rc = -1;

  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(plain_super, sizeof(plain_super));
  vp_wipe(&hdr, sizeof(hdr));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));

  if (block_device_read(chunked_4096, 0u, header_buf) != 0) {
    goto out_preflight;
  }

  out->raw_block_count = chunked_4096->block_count;
  out->target_layout = VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED;
  out->target_visible_blocks = chunked_4096->block_count - 1u;

  if (capyos_volume_header_looks_valid(header_buf)) {
    if (capyos_volume_header_parse(header_buf, &hdr) !=
        CAPYOS_VOLUME_HEADER_OK) {
      goto out_preflight;
    }
    if (hdr.data_offset_lba >= chunked_4096->block_count) {
      goto out_preflight;
    }
    out->status = VOLUME_PROVIDER_REKEY_STATUS_ALREADY_HEADER_MANAGED;
    out->source_layout = VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED;
    out->source_visible_blocks =
        chunked_4096->block_count - hdr.data_offset_lba;
    out->target_visible_blocks = out->source_visible_blocks;
    out->blocker_flags = VOLUME_PROVIDER_REKEY_BLOCK_ALREADY_HEADER_MANAGED;
    rc = 0;
    goto out_preflight;
  }

  if (!legacy_salt || legacy_salt_len == 0u || legacy_iter == 0u) {
    goto out_preflight;
  }

  crypt_derive_xts_keys(password, legacy_salt, legacy_salt_len, legacy_iter,
                        key1, key2);
  legacy_crypt = crypt_init(chunked_4096, key1, key2);
  if (!legacy_crypt || legacy_crypt == chunked_4096) {
    legacy_crypt = NULL;
    goto out_preflight;
  }
  if (block_device_read(legacy_crypt, 0u, plain_super) != 0) {
    goto out_preflight;
  }
  out->status = VOLUME_PROVIDER_REKEY_STATUS_LEGACY_RELOCATION_REQUIRED;
  out->source_layout = VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE;
  out->source_visible_blocks = legacy_crypt->block_count;
  out->capyfs_block_count = vp_get_u32_le(plain_super + 12);
  out->capyfs_data_start = vp_get_u32_le(plain_super + 32);
  if (!vp_capyfs_plain_super_valid(plain_super, out->source_visible_blocks)) {
    goto out_preflight;
  }
  out->action_flags = VOLUME_PROVIDER_REKEY_ACTION_RESERVE_HEADER_LBA0 |
                      VOLUME_PROVIDER_REKEY_ACTION_SHIFT_FS_TO_LBA1 |
                      VOLUME_PROVIDER_REKEY_ACTION_REENCRYPT_WITH_HEADER_KEYS;
  out->blocker_flags =
      VOLUME_PROVIDER_REKEY_BLOCK_RELOCATION_ENGINE_REQUIRED;
  if (out->capyfs_block_count > out->target_visible_blocks) {
    out->action_flags |= VOLUME_PROVIDER_REKEY_ACTION_UPDATE_CAPYFS_GEOMETRY;
    out->blocker_flags |= VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED;
  }
  rc = 0;

out_preflight:
  if (legacy_crypt) {
    crypt_free(legacy_crypt);
  }
  vp_wipe(header_buf, sizeof(header_buf));
  vp_wipe(plain_super, sizeof(plain_super));
  vp_wipe(&hdr, sizeof(hdr));
  vp_wipe(key1, sizeof(key1));
  vp_wipe(key2, sizeof(key2));
  if (rc != 0 && out) {
    vp_wipe(out, sizeof(*out));
  }
  return rc;
}

int volume_provider_rekey_plan(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    struct volume_provider_rekey_plan *out) {
  struct volume_provider_rekey_preflight pf;
  uint32_t target_end = 0;

  if (out) {
    vp_wipe(out, sizeof(*out));
  }
  if (!out) {
    return -1;
  }
  vp_wipe(&pf, sizeof(pf));
  if (volume_provider_rekey_preflight(chunked_4096, password, legacy_salt,
                                      legacy_salt_len, legacy_iter,
                                      &pf) != 0) {
    vp_wipe(out, sizeof(*out));
    vp_wipe(&pf, sizeof(pf));
    return -1;
  }

  out->source_layout = pf.source_layout;
  out->target_layout = pf.target_layout;
  out->action_flags = pf.action_flags;
  out->blocker_flags = 0u;
  out->raw_block_count = pf.raw_block_count;
  out->capyfs_block_count = pf.capyfs_block_count;
  out->copy_direction = VOLUME_PROVIDER_REKEY_COPY_DIRECTION_NONE;

  if (pf.status == VOLUME_PROVIDER_REKEY_STATUS_ALREADY_HEADER_MANAGED) {
    out->status =
        VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED;
    out->blocker_flags =
        VOLUME_PROVIDER_REKEY_BLOCK_ALREADY_HEADER_MANAGED;
    vp_wipe(&pf, sizeof(pf));
    return 0;
  }

  if (pf.status != VOLUME_PROVIDER_REKEY_STATUS_LEGACY_RELOCATION_REQUIRED ||
      pf.capyfs_block_count == 0u) {
    vp_wipe(out, sizeof(*out));
    vp_wipe(&pf, sizeof(pf));
    return -1;
  }

  out->source_first_lba = 0u;
  out->source_last_lba = pf.capyfs_block_count - 1u;
  out->target_first_lba = 1u;
  out->target_last_lba = pf.capyfs_block_count;
  out->blocks_to_reencrypt = pf.capyfs_block_count;
  target_end = out->target_last_lba;

  if ((pf.blocker_flags & VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED) !=
      0u) {
    out->status =
        VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SHRINK_REQUIRED;
    out->blocker_flags =
        VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED;
    vp_wipe(&pf, sizeof(pf));
    return 0;
  }

  if (target_end >= pf.raw_block_count - 1u) {
    out->status =
        VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SCRATCH_REQUIRED;
    out->blocker_flags =
        VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED;
    vp_wipe(&pf, sizeof(pf));
    return 0;
  }

  out->status = VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY;
  out->scratch_first_lba = target_end + 1u;
  out->scratch_available_blocks = pf.raw_block_count - out->scratch_first_lba;
  out->copy_direction = VOLUME_PROVIDER_REKEY_COPY_DIRECTION_REVERSE;
  out->estimated_read_ops = pf.capyfs_block_count;
  if (pf.capyfs_block_count > 0x7FFFFFFFu) {
    out->estimated_write_ops = 0xFFFFFFFFu;
  } else {
    out->estimated_write_ops = (pf.capyfs_block_count * 2u) + 1u;
  }
  vp_wipe(&pf, sizeof(pf));
  return 0;
}

int volume_provider_rekey_execute(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out) {
  struct volume_provider_rekey_plan plan;

  if (out) {
    vp_wipe(out, sizeof(*out));
  }
  if (!out) {
    return -1;
  }
  if (flags != VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN) {
    out->flags = flags;
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED;
    return 0;
  }
  vp_wipe(&plan, sizeof(plan));
  if (volume_provider_rekey_plan(chunked_4096, password, legacy_salt,
                                 legacy_salt_len, legacy_iter, &plan) != 0) {
    vp_wipe(out, sizeof(*out));
    vp_wipe(&plan, sizeof(plan));
    return -1;
  }

  out->flags = flags;
  out->plan_status = plan.status;
  out->blocker_flags = plan.blocker_flags;
  out->blocks_to_reencrypt = plan.blocks_to_reencrypt;
  out->scratch_first_lba = plan.scratch_first_lba;
  out->estimated_read_ops = plan.estimated_read_ops;
  out->estimated_write_ops = plan.estimated_write_ops;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN;

  if (plan.status ==
      VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED) {
    out->status =
        VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED;
    vp_wipe(&plan, sizeof(plan));
    return 0;
  }

  if (plan.status != VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY) {
    out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN;
    vp_wipe(&plan, sizeof(plan));
    return 0;
  }
  out->next_source_lba = plan.source_last_lba;
  out->next_target_lba = plan.target_last_lba;

  out->status = VOLUME_PROVIDER_REKEY_EXEC_STATUS_DRY_RUN_READY;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;
  vp_wipe(&plan, sizeof(plan));
  return 0;
}

int volume_provider_rekey_checkpoint_init(
    const struct volume_provider_rekey_plan *plan,
    uint32_t blocks_completed,
    struct volume_provider_rekey_checkpoint *out) {
  if (out) {
    vp_wipe(out, sizeof(*out));
  }
  if (!plan || !out) {
    return -1;
  }
  if (!vp_rekey_checkpoint_plan_valid(plan) ||
      blocks_completed > plan->blocks_to_reencrypt) {
    return -1;
  }

  out->magic0 = VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC0;
  out->magic1 = VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC1;
  out->version = VOLUME_PROVIDER_REKEY_CHECKPOINT_VERSION;
  out->flags =
      blocks_completed == plan->blocks_to_reencrypt
          ? VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED
          : VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS;
  out->phase_flags = VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN |
                     VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH;
  if (blocks_completed > 0u) {
    out->phase_flags |= VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE;
  }
  if (blocks_completed == plan->blocks_to_reencrypt) {
    out->phase_flags |= VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE |
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER |
                        VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN;
  }
  out->plan_status = plan->status;
  out->source_layout = plan->source_layout;
  out->target_layout = plan->target_layout;
  out->blocker_flags = plan->blocker_flags;
  out->raw_block_count = plan->raw_block_count;
  out->capyfs_block_count = plan->capyfs_block_count;
  out->source_first_lba = plan->source_first_lba;
  out->source_last_lba = plan->source_last_lba;
  out->target_first_lba = plan->target_first_lba;
  out->target_last_lba = plan->target_last_lba;
  out->scratch_first_lba = plan->scratch_first_lba;
  out->blocks_total = plan->blocks_to_reencrypt;
  out->blocks_completed = blocks_completed;
  if (blocks_completed == plan->blocks_to_reencrypt) {
    out->next_source_lba = plan->source_first_lba;
    out->next_target_lba = plan->target_first_lba;
  } else {
    out->next_source_lba = plan->source_last_lba - blocks_completed;
    out->next_target_lba = plan->target_last_lba - blocks_completed;
  }
  out->estimated_read_ops = plan->estimated_read_ops;
  out->estimated_write_ops = plan->estimated_write_ops;
  return 0;
}

int volume_provider_rekey_checkpoint_serialize(
    const struct volume_provider_rekey_checkpoint *checkpoint,
    uint8_t *out,
    size_t out_len) {
  if (!checkpoint || !out ||
      out_len < VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) {
    return -1;
  }
  if (!vp_rekey_checkpoint_semantic_valid(checkpoint)) {
    return -1;
  }

  vp_wipe(out, out_len);
  vp_put_u32_le(out + 0, checkpoint->magic0);
  vp_put_u32_le(out + 4, checkpoint->magic1);
  vp_put_u32_le(out + 8, checkpoint->version);
  vp_put_u32_le(out + 12, checkpoint->flags);
  vp_put_u32_le(out + 16, checkpoint->phase_flags);
  vp_put_u32_le(out + 20, checkpoint->plan_status);
  vp_put_u32_le(out + 24, checkpoint->source_layout);
  vp_put_u32_le(out + 28, checkpoint->target_layout);
  vp_put_u32_le(out + 32, checkpoint->blocker_flags);
  vp_put_u32_le(out + 36, checkpoint->raw_block_count);
  vp_put_u32_le(out + 40, checkpoint->capyfs_block_count);
  vp_put_u32_le(out + 44, checkpoint->source_first_lba);
  vp_put_u32_le(out + 48, checkpoint->source_last_lba);
  vp_put_u32_le(out + 52, checkpoint->target_first_lba);
  vp_put_u32_le(out + 56, checkpoint->target_last_lba);
  vp_put_u32_le(out + 60, checkpoint->scratch_first_lba);
  vp_put_u32_le(out + 64, checkpoint->blocks_total);
  vp_put_u32_le(out + 68, checkpoint->blocks_completed);
  vp_put_u32_le(out + 72, checkpoint->next_source_lba);
  vp_put_u32_le(out + 76, checkpoint->next_target_lba);
  vp_put_u32_le(out + 80, checkpoint->estimated_read_ops);
  vp_put_u32_le(out + 84, checkpoint->estimated_write_ops);
  vp_put_u32_le(out + 124,
                vp_crc32(out, VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE - 4u));
  return 0;
}

int volume_provider_rekey_checkpoint_parse(
    const uint8_t *buf,
    size_t len,
    struct volume_provider_rekey_checkpoint *out) {
  uint32_t stored_crc = 0;
  uint32_t calc_crc = 0;

  if (out) {
    vp_wipe(out, sizeof(*out));
  }
  if (!buf || !out || len < VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE) {
    return -1;
  }

  stored_crc = vp_get_u32_le(buf + 124);
  calc_crc = vp_crc32(buf, VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE - 4u);
  if (stored_crc != calc_crc) {
    return -1;
  }
  for (size_t i = 88u; i < 124u; ++i) {
    if (buf[i] != 0u) {
      return -1;
    }
  }

  out->magic0 = vp_get_u32_le(buf + 0);
  out->magic1 = vp_get_u32_le(buf + 4);
  out->version = vp_get_u32_le(buf + 8);
  out->flags = vp_get_u32_le(buf + 12);
  out->phase_flags = vp_get_u32_le(buf + 16);
  out->plan_status = vp_get_u32_le(buf + 20);
  out->source_layout = vp_get_u32_le(buf + 24);
  out->target_layout = vp_get_u32_le(buf + 28);
  out->blocker_flags = vp_get_u32_le(buf + 32);
  out->raw_block_count = vp_get_u32_le(buf + 36);
  out->capyfs_block_count = vp_get_u32_le(buf + 40);
  out->source_first_lba = vp_get_u32_le(buf + 44);
  out->source_last_lba = vp_get_u32_le(buf + 48);
  out->target_first_lba = vp_get_u32_le(buf + 52);
  out->target_last_lba = vp_get_u32_le(buf + 56);
  out->scratch_first_lba = vp_get_u32_le(buf + 60);
  out->blocks_total = vp_get_u32_le(buf + 64);
  out->blocks_completed = vp_get_u32_le(buf + 68);
  out->next_source_lba = vp_get_u32_le(buf + 72);
  out->next_target_lba = vp_get_u32_le(buf + 76);
  out->estimated_read_ops = vp_get_u32_le(buf + 80);
  out->estimated_write_ops = vp_get_u32_le(buf + 84);
  out->checkpoint_crc32 = stored_crc;

  if (!vp_rekey_checkpoint_semantic_valid(out)) {
    vp_wipe(out, sizeof(*out));
    return -1;
  }
  return 0;
}
