#include "arch/x86_64/storage_boot_provider_policy.h"

#include <stddef.h>

void x64_storage_boot_provider_evaluate(
    const struct x64_storage_boot_provider_input *input,
    struct x64_storage_boot_provider_status *out) {
  if (!out)
    return;
  out->provider_ready = 0u;
  out->reason = X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT;
  if (!input || !input->persistent_mount_ready)
    return;
  if (!input->raw_device_ready) {
    out->reason = X64_STORAGE_BOOT_PROVIDER_NO_RAW_DEVICE;
    return;
  }
  if (!input->data_binding_ready) {
    out->reason = X64_STORAGE_BOOT_PROVIDER_NO_DATA_BINDING;
    return;
  }
  if (!input->esp_binding_ready) {
    out->reason = X64_STORAGE_BOOT_PROVIDER_NO_ESP_BINDING;
    return;
  }
  if (!input->flush_ready) {
    out->reason = X64_STORAGE_BOOT_PROVIDER_NO_FLUSH;
    return;
  }
  out->provider_ready = 1u;
  out->reason = X64_STORAGE_BOOT_PROVIDER_READY;
}

/* Stable ASCII label for every denial reason. Until alpha.319 the reason was
 * only readable through a debugger: `-60` reached the operator with no way to
 * tell a missing flush from an unbound ESP range, which made every external
 * A/B gate failure opaque. `print-boot-slot` now renders this label. */
const char *x64_storage_boot_provider_reason_label(
    enum x64_storage_boot_provider_reason reason) {
  switch (reason) {
  case X64_STORAGE_BOOT_PROVIDER_READY: return "ready";
  case X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT: return "no-persistent-mount";
  case X64_STORAGE_BOOT_PROVIDER_NO_RAW_DEVICE: return "no-raw-device";
  case X64_STORAGE_BOOT_PROVIDER_NO_DATA_BINDING: return "no-data-binding";
  case X64_STORAGE_BOOT_PROVIDER_NO_ESP_BINDING: return "no-esp-binding";
  case X64_STORAGE_BOOT_PROVIDER_NO_FLUSH: return "no-flush";
  case X64_STORAGE_BOOT_PROVIDER_NO_CONTROL: return "no-control";
  case X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN: return "control-unknown";
  case X64_STORAGE_BOOT_PROVIDER_TOKEN_MISMATCH: return "token-mismatch";
  }
  return "unknown";
}

void x64_storage_boot_provider_evaluate_runtime(
    const struct x64_storage_boot_provider_runtime_snapshot *snapshot,
    struct x64_storage_boot_provider_status *out) {
  struct x64_storage_boot_provider_input input = {0};
  if (snapshot) {
    input.persistent_mount_ready = snapshot->persistent_mount_ready;
    input.raw_device_ready =
        snapshot->active_native_backend && snapshot->storage_device_ready &&
                snapshot->raw_block_size == 512u && snapshot->raw_read_ready &&
                snapshot->raw_write_ready
            ? 1u
            : 0u;
    input.data_binding_ready = snapshot->data_binding_verified;
    input.esp_binding_ready = snapshot->esp_binding_verified;
    input.flush_ready = snapshot->flush_ready;
  }
  x64_storage_boot_provider_evaluate(snapshot ? &input : NULL, out);
}

static void policy_zero(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

static int policy_version_len(const char *version) {
  size_t len = 0u;
  if (!version)
    return -1;
  while (len < BOOT_SLOT_VERSION_MAX && version[len]) {
    if (version[len] < 0x20 || version[len] > 0x7E)
      return -1;
    ++len;
  }
  if (len == 0u || len >= BOOT_SLOT_VERSION_MAX)
    return -1;
  return (int)len;
}

static int policy_digest_present(const uint8_t *digest) {
  uint8_t any = 0u;
  if (!digest)
    return 0;
  for (size_t i = 0u; i < BOOT_SLOT_SHA256_SIZE; ++i)
    any |= digest[i];
  return any != 0u;
}

/* Pure selection of the inactive slot plus the image descriptor the store
 * will publish. The caller must pass the same snapshot it will hand to
 * boot_slot_store_stage_inactive_authorized(), so the generational
 * authorization and this plan describe the exact same manager state. */
int x64_storage_boot_provider_plan_stage(
    const struct boot_slot_snapshot *snapshot, const char *version,
    uint32_t payload_size, const uint8_t sha256[BOOT_SLOT_SHA256_SIZE],
    struct x64_storage_boot_stage_plan *out_plan) {
  const struct boot_slot_manager *manager = NULL;
  uint32_t target = 0u;
  int version_len = 0;
  if (out_plan)
    policy_zero(out_plan, sizeof(*out_plan));
  if (!snapshot || !out_plan || !sha256 ||
      snapshot->version != BOOT_SLOT_SNAPSHOT_VERSION ||
      snapshot->size != sizeof(*snapshot) || snapshot->generation == 0u ||
      snapshot->authority_epoch == 0u || snapshot->lease_epoch == 0u ||
      !policy_digest_present(sha256) || payload_size == 0u)
    return -1;
  version_len = policy_version_len(version);
  if (version_len < 0)
    return -1;
  manager = &snapshot->manager;
  if (manager->confirmed_slot >= BOOT_SLOT_COUNT ||
      manager->active_slot != manager->confirmed_slot ||
      manager->pending_slot != BOOT_SLOT_NONE || manager->rollback_pending ||
      manager->tries_remaining != 0u ||
      manager->slots[manager->confirmed_slot].state != BOOT_SLOT_ACTIVE ||
      !manager->slots[manager->confirmed_slot].health_confirmed)
    return -1;
  target = manager->confirmed_slot ^ 1u;
  if (target >= BOOT_SLOT_COUNT ||
      (manager->slots[target].state != BOOT_SLOT_EMPTY &&
       manager->slots[target].state != BOOT_SLOT_VALID &&
       manager->slots[target].state != BOOT_SLOT_FAILED) ||
      manager->slots[target].payload_capacity_sectors == 0u ||
      (uint64_t)payload_size >
          (uint64_t)manager->slots[target].payload_capacity_sectors * 512u)
    return -1;
  out_plan->slot = target;
  for (int i = 0; i < version_len; ++i)
    out_plan->image.version[i] = version[i];
  out_plan->image.payload_size = payload_size;
  for (size_t i = 0u; i < BOOT_SLOT_SHA256_SIZE; ++i)
    out_plan->image.payload_sha256[i] = sha256[i];
  return 0;
}

int x64_storage_boot_provider_range_u32(uint64_t start, uint64_t count,
                                        uint32_t *out_start,
                                        uint32_t *out_count) {
  if (out_start)
    *out_start = 0u;
  if (out_count)
    *out_count = 0u;
  if (!out_start || !out_count || count == 0u || start > UINT32_MAX ||
      count > UINT32_MAX || start + count < start ||
      start + count > (uint64_t)UINT32_MAX + 1u)
    return -1;
  *out_start = (uint32_t)start;
  *out_count = (uint32_t)count;
  return 0;
}

static int policy_guid_equal(const uint8_t a[16], const uint8_t b[16]) {
  uint8_t diff = 0u;
  if (!a || !b)
    return 0;
  for (size_t i = 0u; i < 16u; ++i)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0u;
}

static int policy_guid_present(const uint8_t guid[16]) {
  uint8_t any = 0u;
  if (!guid)
    return 0;
  for (size_t i = 0u; i < 16u; ++i)
    any |= guid[i];
  return any != 0u;
}

int x64_storage_boot_provider_select_data(
    int gpt_ready, uint32_t gpt_start, uint32_t gpt_count,
    int handoff_ready, uint32_t handoff_start, uint32_t handoff_count,
    uint32_t *out_start, uint32_t *out_count, int *out_verified) {
  if (out_start)
    *out_start = 0u;
  if (out_count)
    *out_count = 0u;
  if (out_verified)
    *out_verified = 0;
  if (!out_start || !out_count || !out_verified)
    return -1;
  if (gpt_ready) {
    if (gpt_count == 0u ||
        (handoff_ready &&
         (gpt_start != handoff_start || gpt_count != handoff_count)))
      return -1;
    *out_start = gpt_start;
    *out_count = gpt_count;
    *out_verified = 1;
    return 0;
  }
  if (!handoff_ready || handoff_count == 0u)
    return -1;
  *out_start = handoff_start;
  *out_count = handoff_count;
  return 0;
}

int x64_storage_boot_provider_identity_matches_handoff(
    const struct boot_handoff *handoff,
    const struct capyos_gpt_identity *identity, int identity_strict) {
  uint32_t required_flag = identity_strict
                               ? BOOT_HANDOFF_DISK_IDENTITY_VALID
                               : BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID;
  if (!handoff || !identity || handoff->version < 9u ||
      handoff->disk_identity.flags != required_flag ||
      handoff->efi_disk_last_lba_raw == UINT64_MAX ||
      (uint64_t)identity->disk_sectors !=
          handoff->efi_disk_last_lba_raw + 1u)
    return 0;
  return handoff->disk_identity.esp_lba_start == identity->esp.lba &&
         handoff->disk_identity.esp_lba_count == identity->esp.sectors &&
         handoff->disk_identity.boot_lba_start == identity->boot.lba &&
         handoff->disk_identity.boot_lba_count == identity->boot.sectors &&
         handoff->disk_identity.data_lba_start == identity->data.lba &&
         handoff->disk_identity.data_lba_count == identity->data.sectors &&
         policy_guid_equal(handoff->disk_identity.disk_guid,
                           identity->disk_guid) &&
         policy_guid_equal(handoff->disk_identity.esp_partition_guid,
                           identity->esp.guid) &&
         policy_guid_equal(handoff->disk_identity.boot_partition_guid,
                           identity->boot.guid) &&
         policy_guid_equal(handoff->disk_identity.data_partition_guid,
                           identity->data.guid);
}

int x64_storage_boot_attempt_from_handoff(
    const struct boot_handoff *handoff,
    struct boot_slot_attempt_handoff *out_attempt) {
  uint32_t flags;
  if (out_attempt)
    *out_attempt = (struct boot_slot_attempt_handoff){0};
  if (!handoff || !out_attempt || handoff->version < 10u)
    return -1;
  flags = handoff->slot_attempt.flags;
  if ((flags != BOOT_HANDOFF_SLOT_ATTEMPT_VALID &&
       flags != (BOOT_HANDOFF_SLOT_ATTEMPT_VALID |
                 BOOT_HANDOFF_SLOT_ATTEMPT_PENDING) &&
       flags != (BOOT_HANDOFF_SLOT_ATTEMPT_VALID |
                 BOOT_HANDOFF_SLOT_ATTEMPT_ROLLBACK)) ||
      handoff->slot_attempt.slot >= BOOT_SLOT_COUNT ||
      handoff->slot_attempt.generation == 0u ||
      handoff->disk_identity.flags != BOOT_HANDOFF_DISK_IDENTITY_VALID)
    return -1;
  *out_attempt = handoff->slot_attempt;
  return 0;
}

int x64_storage_boot_provider_binding_from_handoff(
    const struct boot_handoff *handoff,
    struct boot_slot_disk_binding *out_binding) {
  const struct boot_disk_identity *identity = NULL;
  struct boot_slot_disk_binding candidate;
  uint64_t raw_last_lba = 0u;
  if (out_binding) {
    uint8_t *bytes = (uint8_t *)out_binding;
    for (size_t i = 0u; i < sizeof(*out_binding); ++i)
      bytes[i] = 0u;
  }
  if (!handoff || !out_binding || handoff->version < 9u)
    return -1;
  identity = &handoff->disk_identity;
  raw_last_lba = handoff->efi_disk_last_lba_raw;
  {
    uint8_t *bytes = (uint8_t *)&candidate;
    for (size_t i = 0u; i < sizeof(candidate); ++i)
      bytes[i] = 0u;
  }
  if (identity->flags != BOOT_HANDOFF_DISK_IDENTITY_VALID ||
      identity->reserved != 0u ||
      x64_storage_boot_provider_range_u32(
          identity->esp_lba_start, identity->esp_lba_count,
          &candidate.esp_lba, &candidate.esp_sectors) != 0 ||
      x64_storage_boot_provider_range_u32(
          identity->boot_lba_start, identity->boot_lba_count,
          &candidate.boot_lba, &candidate.boot_sectors) != 0 ||
      x64_storage_boot_provider_range_u32(
          identity->data_lba_start, identity->data_lba_count,
          &candidate.data_lba, &candidate.data_sectors) != 0 ||
      !policy_guid_present(identity->disk_guid) ||
      !policy_guid_present(identity->esp_partition_guid) ||
      !policy_guid_present(identity->boot_partition_guid) ||
      !policy_guid_present(identity->data_partition_guid) ||
      policy_guid_equal(identity->disk_guid, identity->esp_partition_guid) ||
      policy_guid_equal(identity->disk_guid, identity->boot_partition_guid) ||
      policy_guid_equal(identity->disk_guid, identity->data_partition_guid) ||
      policy_guid_equal(identity->esp_partition_guid,
                        identity->boot_partition_guid) ||
      policy_guid_equal(identity->esp_partition_guid,
                        identity->data_partition_guid) ||
      policy_guid_equal(identity->boot_partition_guid,
                        identity->data_partition_guid) ||
      raw_last_lba == 0u ||
      identity->esp_lba_start + identity->esp_lba_count - 1u > raw_last_lba ||
      identity->boot_lba_start + identity->boot_lba_count - 1u > raw_last_lba ||
      identity->data_lba_start + identity->data_lba_count - 1u > raw_last_lba ||
      identity->esp_lba_start + identity->esp_lba_count >
          identity->boot_lba_start ||
      identity->boot_lba_start + identity->boot_lba_count >
          identity->data_lba_start ||
      handoff->data_lba_start_raw != identity->data_lba_start ||
      handoff->data_lba_count_raw != identity->data_lba_count)
    return -1;
  for (size_t i = 0u; i < 16u; ++i) {
    candidate.disk_guid[i] = identity->disk_guid[i];
    candidate.esp_guid[i] = identity->esp_partition_guid[i];
    candidate.boot_guid[i] = identity->boot_partition_guid[i];
    candidate.data_guid[i] = identity->data_partition_guid[i];
  }
  *out_binding = candidate;
  return 0;
}
