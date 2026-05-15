#ifndef CAPYOS_SECURITY_VOLUME_PROVIDER_H
#define CAPYOS_SECURITY_VOLUME_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"

#define VOLUME_PROVIDER_LAYOUT_UNKNOWN 0u
#define VOLUME_PROVIDER_LAYOUT_HEADER_MANAGED 1u
#define VOLUME_PROVIDER_LAYOUT_LEGACY_FULL_DEVICE 2u

#define VOLUME_PROVIDER_REKEY_STATUS_UNKNOWN 0u
#define VOLUME_PROVIDER_REKEY_STATUS_ALREADY_HEADER_MANAGED 1u
#define VOLUME_PROVIDER_REKEY_STATUS_LEGACY_RELOCATION_REQUIRED 2u

#define VOLUME_PROVIDER_REKEY_ACTION_RESERVE_HEADER_LBA0 0x00000001u
#define VOLUME_PROVIDER_REKEY_ACTION_SHIFT_FS_TO_LBA1 0x00000002u
#define VOLUME_PROVIDER_REKEY_ACTION_REENCRYPT_WITH_HEADER_KEYS 0x00000004u
#define VOLUME_PROVIDER_REKEY_ACTION_UPDATE_CAPYFS_GEOMETRY 0x00000008u

#define VOLUME_PROVIDER_REKEY_BLOCK_ALREADY_HEADER_MANAGED 0x00000001u
#define VOLUME_PROVIDER_REKEY_BLOCK_RELOCATION_ENGINE_REQUIRED 0x00000002u
#define VOLUME_PROVIDER_REKEY_BLOCK_CAPYFS_SHRINK_REQUIRED 0x00000004u
#define VOLUME_PROVIDER_REKEY_BLOCK_TRANSACTION_SCRATCH_REQUIRED 0x00000008u

#define VOLUME_PROVIDER_REKEY_PLAN_STATUS_UNKNOWN 0u
#define VOLUME_PROVIDER_REKEY_PLAN_STATUS_READY 1u
#define VOLUME_PROVIDER_REKEY_PLAN_STATUS_ALREADY_HEADER_MANAGED 2u
#define VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SHRINK_REQUIRED 3u
#define VOLUME_PROVIDER_REKEY_PLAN_STATUS_BLOCKED_SCRATCH_REQUIRED 4u

#define VOLUME_PROVIDER_REKEY_COPY_DIRECTION_NONE 0u
#define VOLUME_PROVIDER_REKEY_COPY_DIRECTION_REVERSE 1u

#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN 0x00000001u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE \
  0x00000002u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE 0x00000004u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP 0x00000008u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER 0x00000010u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP 0x00000020u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH 0x00000040u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ORCHESTRATE 0x00000080u
#define VOLUME_PROVIDER_REKEY_EXEC_FLAG_ORCHESTRATE_ABORT 0x00000100u

#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_UNKNOWN 0u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_DRY_RUN_READY 1u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ALREADY_HEADER_MANAGED 2u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_BLOCKED_BY_PLAN 3u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_WRITES_DISABLED 4u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITTEN 5u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITE_FAILED 6u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_VERIFY_FAILED 7u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN 8u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED 9u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITE_FAILED 10u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_VERIFY_FAILED 11u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE 12u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE 13u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED 14u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_READ_FAILED 15u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_WRITE_FAILED 16u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_VERIFY_FAILED 17u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED 18u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITTEN 19u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED 20u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITE_FAILED 21u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_VERIFY_FAILED 22u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_OPEN_FAILED 23u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_CHECKPOINT_FAILED 24u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_STEP_DONE 25u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_COMPLETE 26u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_BUILD_FAILED 27u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_READ_FAILED 28u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_WRITE_FAILED 29u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_VERIFY_FAILED 30u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ROLLBACK_CHECKPOINT_FAILED 31u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_DONE 32u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_NOT_NEEDED 33u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_BUILD_FAILED 34u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_WRITE_FAILED 35u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_CLEANUP_SCRATCH_VERIFY_FAILED 36u
#define VOLUME_PROVIDER_REKEY_EXEC_STATUS_ORCHESTRATE_BUILD_FAILED 37u

#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_VALIDATE_PLAN 0x00000001u
#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_CHECKPOINT_SCRATCH 0x00000002u
#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_COPY_REVERSE 0x00000004u
#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_COMMIT_HEADER 0x00000008u
#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_VERIFY_OPEN 0x00000010u
#define VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER 0x00000020u

#define VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC0 0x59504143u
#define VOLUME_PROVIDER_REKEY_CHECKPOINT_MAGIC1 0x50434B52u
#define VOLUME_PROVIDER_REKEY_CHECKPOINT_VERSION 1u
#define VOLUME_PROVIDER_REKEY_CHECKPOINT_SIZE 128u
#define VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_IN_PROGRESS 0x00000001u
#define VOLUME_PROVIDER_REKEY_CHECKPOINT_FLAG_COMPLETED 0x00000002u

#define VOLUME_PROVIDER_REKEY_STAGE_MAGIC0 0x59504143u
#define VOLUME_PROVIDER_REKEY_STAGE_MAGIC1 0x48475453u
#define VOLUME_PROVIDER_REKEY_STAGE_VERSION 1u
#define VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_SIZE 128u
#define VOLUME_PROVIDER_REKEY_STAGE_CHECKPOINT_OFFSET 0u
#define VOLUME_PROVIDER_REKEY_STAGE_HEADER_OFFSET 512u
#define VOLUME_PROVIDER_REKEY_STAGE_HEADER_SIZE 512u
#define VOLUME_PROVIDER_REKEY_STAGE_MANIFEST_OFFSET 1024u

struct volume_provider_rekey_preflight {
  uint32_t status;
  uint32_t source_layout;
  uint32_t target_layout;
  uint32_t action_flags;
  uint32_t blocker_flags;
  uint32_t raw_block_count;
  uint32_t source_visible_blocks;
  uint32_t target_visible_blocks;
  uint32_t capyfs_block_count;
  uint32_t capyfs_data_start;
};

struct volume_provider_rekey_plan {
  uint32_t status;
  uint32_t source_layout;
  uint32_t target_layout;
  uint32_t action_flags;
  uint32_t blocker_flags;
  uint32_t raw_block_count;
  uint32_t capyfs_block_count;
  uint32_t source_first_lba;
  uint32_t source_last_lba;
  uint32_t target_first_lba;
  uint32_t target_last_lba;
  uint32_t scratch_first_lba;
  uint32_t scratch_available_blocks;
  uint32_t blocks_to_reencrypt;
  uint32_t copy_direction;
  uint32_t estimated_read_ops;
  uint32_t estimated_write_ops;
};

struct volume_provider_rekey_execution_report {
  uint32_t status;
  uint32_t flags;
  uint32_t plan_status;
  uint32_t phase_flags;
  uint32_t blocker_flags;
  uint32_t blocks_to_reencrypt;
  uint32_t scratch_first_lba;
  uint32_t next_source_lba;
  uint32_t next_target_lba;
  uint32_t estimated_read_ops;
  uint32_t estimated_write_ops;
  uint32_t blocks_completed;
  uint32_t blocks_remaining;
};

struct volume_provider_rekey_checkpoint {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t version;
  uint32_t flags;
  uint32_t phase_flags;
  uint32_t plan_status;
  uint32_t source_layout;
  uint32_t target_layout;
  uint32_t blocker_flags;
  uint32_t raw_block_count;
  uint32_t capyfs_block_count;
  uint32_t source_first_lba;
  uint32_t source_last_lba;
  uint32_t target_first_lba;
  uint32_t target_last_lba;
  uint32_t scratch_first_lba;
  uint32_t blocks_total;
  uint32_t blocks_completed;
  uint32_t next_source_lba;
  uint32_t next_target_lba;
  uint32_t estimated_read_ops;
  uint32_t estimated_write_ops;
  uint32_t checkpoint_crc32;
};

struct volume_provider_rekey_stage_manifest {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t version;
  uint32_t flags;
  uint32_t checkpoint_offset;
  uint32_t checkpoint_size;
  uint32_t staged_header_offset;
  uint32_t staged_header_size;
  uint32_t manifest_offset;
  uint32_t manifest_size;
  uint32_t scratch_lba;
  uint32_t raw_block_count;
  uint32_t capyfs_block_count;
  uint32_t source_first_lba;
  uint32_t source_last_lba;
  uint32_t target_first_lba;
  uint32_t target_last_lba;
  uint32_t blocks_total;
  uint32_t checkpoint_crc32;
  uint32_t staged_header_crc32;
  uint32_t manifest_crc32;
};

/*
 * CapyOS volume provider (alpha.222-alpha.228).
 *
 * Glue layer that wires the alpha.221 on-disk volume header into the
 * installer (write-side) and the kernel boot path (read-side). It is
 * the ONLY module besides the legacy `crypt_derive_xts_keys` callers
 * that knows about the per-volume layout of the encrypted partition.
 *
 * Layout on disk (header-managed volumes, alpha.222+):
 *
 *   chunked_4096[0]   - 4 KiB block whose first 512 bytes are the
 *                       `struct capyos_volume_header`; the remaining
 *                       3584 bytes are zero (reserved for future
 *                       header expansion).
 *   chunked_4096[1..] - encrypted filesystem (AES-XTS by the keys the
 *                       header binds to a per-install salt + Argon2id
 *                       parameters).
 *
 * Layout on disk (legacy volumes, pre-alpha.222):
 *
 *   chunked_4096[0..] - encrypted filesystem directly (no header).
 *                       AES-XTS keys derived via PBKDF2-SHA256 against
 *                       the kernel-baked `g_disk_salt` constant and
 *                       16000 iterations.
 *
 * The boot path distinguishes the two by attempting to parse a header
 * at chunked[0] via `capyos_volume_header_looks_valid`. The probability
 * of legacy random ciphertext satisfying both magic words and CRC32
 * is ~2^-96 — effectively zero in practice.
 *
 * Threat model
 * ------------
 *   Header-managed volumes inherit the threat model documented in
 *   `include/security/volume_header.h`:
 *
 *     - `kdf_check_tag` (HMAC-SHA256) cryptographically binds the
 *       per-install salt, KDF parameters, and data offset to the
 *       derived AES-XTS keys. An attacker who rewrites any of those
 *       fields forces a different key, the tag mismatches, mount
 *       refuses.
 *     - `header_crc32` is a bit-rot gate, NOT a security primitive.
 *
 *   The provider does NOT distinguish "wrong password" from "tampered
 *   header" in its return value. Both surface as a generic failure
 *   (-1, *out_crypt = NULL) so the calling UX cannot accidentally
 *   become an oracle for header tampering.
 *
 *   When `looks_valid` returns 0 (no header), the provider falls back
 *   to the legacy PBKDF2 path. This is safe because:
 *
 *     - Legacy volumes have no on-disk authentication tag, so the
 *       worst a wrong password can do is silently mount garbage and
 *       fail at the CAPYFS magic check inside `mount_capyfs`. Same
 *       behaviour as pre-alpha.222.
 *     - A modern header-managed volume CANNOT downgrade to legacy
 *       because `looks_valid` would return 1, locking the path to
 *       the modern header check.
 */

/*
 * Initialise a fresh encrypted volume with a per-install random salt
 * and write the on-disk header at LBA 0 of the chunked 4 KiB device.
 *
 * Requirements:
 *   - `chunked_4096->block_size == 4096`
 *   - `chunked_4096->block_count >= 2` (1 for header + at least 1 for FS)
 *   - `password` must be a NUL-terminated C string
 *
 * Side effects on success:
 *   - 16-byte salt generated via the kernel CSPRNG.
 *   - `capyos_volume_header` populated with Argon2id default parameters
 *     (`CRYPT_VOLUME_ARGON2ID_T_COST` / `CRYPT_VOLUME_ARGON2ID_M_COST`).
 *   - AES-XTS keys derived via `crypt_derive_xts_keys_argon2id`.
 *   - `kdf_check_tag` computed (HMAC-SHA256 over context + header prefix).
 *   - `header_crc32` finalized.
 *   - 4 KiB block written to `chunked_4096[0]` (first 512 bytes are
 *     the serialised header, remaining 3584 bytes are zero).
 *   - `*out_crypt` points at a new crypt-wrapped offset device that
 *     starts at `chunked_4096[1]` (the FS area). Caller owns it and
 *     must `crypt_free` on tear-down.
 *
 * Returns 0 on success, -1 on any failure. On failure `*out_crypt` is
 * set to NULL and all key material is wiped from the local frame.
 */
int volume_provider_install(struct block_device *chunked_4096,
                            const char *password,
                            struct block_device **out_crypt);

/*
 * Open an existing encrypted volume. Tries the header-managed path
 * first, falls back to the legacy PBKDF2 path when no header is on
 * disk.
 *
 * Header-managed path:
 *   - Reads `chunked_4096[0]`.
 *   - Runs `capyos_volume_header_looks_valid` as a cheap gate.
 *   - If gate passes, parses the header, derives keys via the
 *     header-declared KDF, verifies the `kdf_check_tag`, wraps
 *     `chunked_4096[hdr.data_offset_lba..]` with crypt_init.
 *   - On ANY failure inside the header-managed path (parse, derive,
 *     tag mismatch), returns -1 WITHOUT falling back to legacy. This
 *     is the documented contract — header-managed volumes are
 *     authoritatively header-managed, and silently downgrading to
 *     legacy on a wrong password would mount unrelated ciphertext.
 *
 * Legacy path (when looks_valid == 0):
 *   - Derives keys via `crypt_derive_xts_keys(password, legacy_salt,
 *     legacy_salt_len, legacy_iter, ...)`.
 *   - Wraps the entire `chunked_4096` device with crypt_init (the FS
 *     starts at block 0 in legacy layouts).
 *
 * `legacy_salt`/`legacy_salt_len`/`legacy_iter` come from the kernel's
 * `g_disk_salt` constant and `g_kdf_iterations`. The provider does not
 * embed those itself to keep this module self-contained and testable
 * with arbitrary legacy parameters.
 *
 * Returns 0 + `*out_crypt` on success, -1 + `*out_crypt = NULL` on
 * authentication failure or I/O error. Wipes all key material from
 * the local frame before returning.
 */
int volume_provider_open(struct block_device *chunked_4096,
                         const char *password,
                         const uint8_t *legacy_salt,
                         size_t legacy_salt_len,
                         uint32_t legacy_iter,
                         struct block_device **out_crypt);

/*
 * Alpha.223 migration preflight. This is intentionally read-only: it
 * classifies the current disk layout and reports the relocation work
 * required before any legacy volume can become header-managed.
 *
 * A legacy pre-alpha.222 CAPYFS volume starts at raw LBA 0. A modern
 * header-managed volume reserves raw LBA 0 for `CAPYVHDR` and starts
 * CAPYFS at raw LBA 1. Therefore a safe re-key is NOT just "write a
 * header": it must decrypt the legacy filesystem, re-encrypt every
 * visible CAPYFS block under the header-bound keys, shift the filesystem
 * right by one LBA, and possibly shrink/update CAPYFS geometry when the
 * legacy superblock consumes the full raw device.
 *
 * Returns 0 when a layout was classified into `out`, -1 on invalid
 * input, I/O failure, bad legacy password, or unsupported plaintext.
 * On failure `out` is zeroed. No disk blocks are written.
 */
int volume_provider_rekey_preflight(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    struct volume_provider_rekey_preflight *out);

/*
 * Alpha.224 transaction planner. This remains read-only but goes one
 * step beyond preflight: it turns a classified legacy CAPYFS volume
 * into a deterministic relocation/re-encryption plan that a future
 * executor can apply in reverse-copy order.
 *
 * The planner refuses to mark a migration READY unless there is at
 * least one raw block after the target CAPYFS range that can be used
 * as transactional scratch. Without that scratch block, a power loss
 * after overwriting raw source blocks would leave neither the legacy
 * layout nor the header-managed target fully recoverable.
 *
 * Returns 0 when a plan or explicit blocker was produced, -1 on
 * invalid input or failed preflight. On failure `out` is zeroed.
 */
int volume_provider_rekey_plan(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    struct volume_provider_rekey_plan *out);

/*
 * Alpha.225 guarded transaction executor contract. This API deliberately
 * supports dry-run only in alpha.225. Any caller that omits
 * `VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN` or combines it with
 * unknown flags receives an explicit WRITES_DISABLED status before
 * planning/password derivation and no disk blocks are written.
 *
 * The dry-run path validates the alpha.224 plan and returns the
 * transaction phases a future write-enabled executor must perform:
 * validate plan, checkpoint scratch, stage target header, reverse
 * copy/re-encrypt, commit header last, then verify open.
 *
 * Returns 0 when an execution status was produced. In the dry-run path,
 * returns -1 on invalid input or failed planning. On failure `out` is
 * zeroed.
 */
int volume_provider_rekey_execute(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

/*
 * Alpha.226 persistent migration checkpoint contract. These helpers are
 * pure and perform no I/O: the future write-enabled executor serializes
 * this 128-byte record into the transaction scratch block before copying
 * data, then parses it after interruption to decide whether resume,
 * rollback or abort is safe.
 *
 * The serialized form is little-endian, CRC32-protected against bit rot,
 * and reserves bytes [88..124) as all-zero expansion space. CRC32 is not
 * a security primitive; it prevents accidental torn/corrupt checkpoints
 * from being trusted. The record is accepted only for alpha.224 READY
 * reverse-copy plans with no blockers and coherent next-source/target
 * progress. The alpha.228 STAGE_HEADER bit is an additive pre-copy phase and
 * may be present before any filesystem block has been moved.
 */
int volume_provider_rekey_checkpoint_init(
    const struct volume_provider_rekey_plan *plan,
    uint32_t blocks_completed,
    struct volume_provider_rekey_checkpoint *out);

int volume_provider_rekey_checkpoint_serialize(
    const struct volume_provider_rekey_checkpoint *checkpoint,
    uint8_t *out,
    size_t out_len);

int volume_provider_rekey_checkpoint_parse(
    const uint8_t *buf,
    size_t len,
    struct volume_provider_rekey_checkpoint *out);

/*
 * Alpha.228 scratch-stage manifest contract. The manifest is a 128-byte
 * little-endian record stored inside the scratch block alongside the
 * checkpoint and staged target header. It records offsets, sizes, source/target
 * ranges and CRC32 gates for the staged checkpoint/header so a future
 * resume/abort path can reject torn scratch state before touching data blocks.
 */
int volume_provider_rekey_stage_manifest_serialize(
    const struct volume_provider_rekey_stage_manifest *manifest,
    uint8_t *out,
    size_t out_len);

int volume_provider_rekey_stage_manifest_parse(
    const uint8_t *buf,
    size_t len,
    struct volume_provider_rekey_stage_manifest *out);

/*
 * Alpha.227 guarded checkpoint writer. This is the first write-enabled
 * migration step, but it writes ONLY the alpha.226 checkpoint record into
 * the planner-selected scratch block and verifies it by reading/parsing it
 * back. It never copies filesystem blocks and never commits a header.
 *
 * Callers must pass exactly
 * `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE`; every
 * other flag combination returns WRITES_DISABLED before password derivation
 * or planning. CHECKPOINT_WRITTEN means the scratch checkpoint is durable
 * enough for the future copy/re-encrypt phase to start; write/verify failure
 * statuses mean no destructive migration phase may run.
 */
int volume_provider_rekey_execute_checkpoint(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

/*
 * Alpha.228 staged-header writer. This advances the write-enabled executor
 * without copying filesystem data: it persists one scratch block containing
 * checkpoint[0..128), a complete candidate alpha.221 volume header at offset
 * 512, and a 128-byte stage manifest at offset 1024. The staged header is
 * Argon2id-backed with a fresh CSPRNG salt and a verified kdf_check_tag.
 *
 * The API requires exactly
 * `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE`; every other
 * flag combination returns WRITES_DISABLED before password derivation/planning.
 * Success means the future copy/re-encrypt phase has durable target-header
 * identity in scratch and reports STAGE_HEADER in the phase flags, but LBA0
 * remains untouched and no filesystem block is copied or re-encrypted yet.
 */
int volume_provider_rekey_execute_stage_header(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

/*
 * Alpha.229 guarded reverse-copy step. This advances exactly one filesystem
 * block from the legacy full-device encryption domain into the staged
 * header-managed target domain, in reverse order. It verifies the target
 * plaintext through the new header keys before updating checkpoint+manifest in
 * scratch. LBA0 remains untouched; the final header commit is a later phase.
 */
int volume_provider_rekey_execute_copy_step(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

/*
 * Alpha.230 guarded final header commit. This is the only rekey executor step
 * allowed to write LBA0, and only after scratch proves every planned block was
 * copied and verified. It writes the staged header to LBA0, verifies the header
 * by read-back and volume open, then marks the checkpoint completed in scratch.
 */
int volume_provider_rekey_execute_commit_header(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

int volume_provider_rekey_execute_rollback_step(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

int volume_provider_rekey_execute_cleanup_scratch(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

int volume_provider_rekey_execute_orchestrated_step(
    struct block_device *chunked_4096,
    const char *password,
    const uint8_t *legacy_salt,
    size_t legacy_salt_len,
    uint32_t legacy_iter,
    uint32_t flags,
    struct volume_provider_rekey_execution_report *out);

#endif /* CAPYOS_SECURITY_VOLUME_PROVIDER_H */
