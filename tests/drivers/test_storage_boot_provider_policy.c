#include <stdio.h>
#include <string.h>

#include "arch/x86_64/storage_boot_provider_policy.h"

static int policy_expect(const struct x64_storage_boot_provider_input *input,
                         int ready,
                         enum x64_storage_boot_provider_reason reason,
                         const char *name) {
  struct x64_storage_boot_provider_status status;
  x64_storage_boot_provider_evaluate(input, &status);
  if (status.provider_ready != (uint8_t)(ready ? 1u : 0u) ||
      status.reason != reason) {
    fprintf(stderr, "[storage-boot-provider] %s\n", name);
    return 1;
  }
  return 0;
}

int test_storage_boot_provider_policy_run(void) {
  struct x64_storage_boot_provider_input input = {0};
  int fails = 0;

  fails += policy_expect(NULL, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT,
                         "NULL input");
  fails += policy_expect(&input, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT,
                         "persistent mount required");
  input.persistent_mount_ready = 1u;
  fails += policy_expect(&input, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_RAW_DEVICE,
                         "raw device required");
  input.raw_device_ready = 1u;
  fails += policy_expect(&input, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_DATA_BINDING,
                         "DATA binding required");
  input.data_binding_ready = 1u;
  fails += policy_expect(&input, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_ESP_BINDING,
                         "ESP binding required");
  input.esp_binding_ready = 1u;
  fails += policy_expect(&input, 0,
                         X64_STORAGE_BOOT_PROVIDER_NO_FLUSH,
                         "durable flush required");
  input.flush_ready = 1u;
  fails += policy_expect(&input, 1, X64_STORAGE_BOOT_PROVIDER_READY,
                         "complete capability accepted");

  {
    struct x64_storage_boot_provider_runtime_snapshot snapshot = {0};
    struct x64_storage_boot_provider_status status;
    snapshot.persistent_mount_ready = 1u;
    snapshot.active_native_backend = 1u;
    snapshot.storage_device_ready = 1u;
    snapshot.data_binding_verified = 1u;
    snapshot.esp_binding_verified = 1u;
    snapshot.flush_ready = 1u;
    snapshot.raw_read_ready = 1u;
    snapshot.raw_write_ready = 1u;
    snapshot.raw_block_size = 4096u;
    x64_storage_boot_provider_evaluate_runtime(&snapshot, &status);
    fails += status.reason == X64_STORAGE_BOOT_PROVIDER_NO_RAW_DEVICE ? 0 : 1;
    snapshot.raw_block_size = 512u;
    snapshot.esp_binding_verified = 0u;
    x64_storage_boot_provider_evaluate_runtime(&snapshot, &status);
    fails += status.reason == X64_STORAGE_BOOT_PROVIDER_NO_ESP_BINDING ? 0 : 1;
    snapshot.esp_binding_verified = 1u;
    snapshot.flush_ready = 0u;
    x64_storage_boot_provider_evaluate_runtime(&snapshot, &status);
    fails += status.reason == X64_STORAGE_BOOT_PROVIDER_NO_FLUSH ? 0 : 1;
    snapshot.flush_ready = 1u;
    x64_storage_boot_provider_evaluate_runtime(&snapshot, &status);
    fails += status.provider_ready ? 0 : 1;
  }

  {
    uint32_t start = 0u;
    uint32_t count = 0u;
    int verified = 0;
    fails += x64_storage_boot_provider_range_u32(
                 (uint64_t)UINT32_MAX + 1u, 1u, &start, &count) != 0
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_range_u32(
                 100u, 50u, &start, &count) == 0 && start == 100u && count == 50u
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_select_data(
                 1, 100u, 50u, 1, 101u, 50u,
                 &start, &count, &verified) != 0
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_select_data(
                 1, 100u, 50u, 1, 100u, 50u,
                 &start, &count, &verified) == 0 && verified &&
                     start == 100u && count == 50u
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_select_data(
                 0, 0u, 0u, 1, 100u, 50u,
                 &start, &count, &verified) == 0 && !verified
                 ? 0
                 : 1;
  }

  {
    struct boot_handoff handoff = {0};
    struct boot_slot_disk_binding binding;
    handoff.version = 9u;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID;
    handoff.disk_identity.esp_lba_start = 40u;
    handoff.disk_identity.esp_lba_count = 10u;
    handoff.disk_identity.boot_lba_start = 50u;
    handoff.disk_identity.boot_lba_count = 20u;
    handoff.disk_identity.data_lba_start = 70u;
    handoff.disk_identity.data_lba_count = 91u;
    handoff.efi_disk_last_lba_raw = 199u;
    handoff.data_lba_start_raw = 70u;
    handoff.data_lba_count_raw = 91u;
    handoff.disk_identity.disk_guid[0] = 1u;
    handoff.disk_identity.esp_partition_guid[0] = 2u;
    handoff.disk_identity.boot_partition_guid[0] = 3u;
    handoff.disk_identity.data_partition_guid[0] = 4u;
    fails += x64_storage_boot_provider_binding_from_handoff(
                 &handoff, &binding) == 0 && binding.boot_lba == 50u &&
                     binding.data_sectors == 91u
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID |
                                  BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID;
    memset(&binding, 0xA5, sizeof(binding));
    fails += x64_storage_boot_provider_binding_from_handoff(
                 &handoff, &binding) != 0 && binding.boot_lba == 0u &&
                     binding.disk_guid[0] == 0u
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID | 0x80000000u;
    memset(&binding, 0xA5, sizeof(binding));
    fails += x64_storage_boot_provider_binding_from_handoff(
                 &handoff, &binding) != 0 && binding.boot_lba == 0u &&
                     binding.disk_guid[0] == 0u
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID;
    {
      uint8_t legacy_bytes[BOOT_HANDOFF_V8_SIZE];
      struct boot_handoff upgraded = {0};
      memset(legacy_bytes, 0x5Au, sizeof(legacy_bytes));
      memcpy(&upgraded, legacy_bytes, sizeof(legacy_bytes));
      upgraded.magic = BOOT_HANDOFF_MAGIC;
      upgraded.version = 8u;
      memset(&binding, 0xA5, sizeof(binding));
      fails += x64_storage_boot_provider_binding_from_handoff(
                   &upgraded, &binding) != 0 && binding.esp_lba == 0u &&
                       binding.disk_guid[0] == 0u
                   ? 0
                   : 1;
    }
    handoff.version = 8u;
    fails += x64_storage_boot_provider_binding_from_handoff(
                 &handoff, &binding) != 0
                 ? 0
                 : 1;
    handoff.version = 9u;
    handoff.disk_identity.esp_partition_guid[0] = 0u;
    fails += x64_storage_boot_provider_binding_from_handoff(
                 &handoff, &binding) != 0
                 ? 0
                 : 1;
  }

  {
    struct boot_handoff handoff = {0};
    struct boot_slot_attempt_handoff attempt;
    handoff.version = 10u;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID;
    handoff.slot_attempt.flags = BOOT_HANDOFF_SLOT_ATTEMPT_VALID |
                                 BOOT_HANDOFF_SLOT_ATTEMPT_PENDING;
    handoff.slot_attempt.slot = 1u;
    handoff.slot_attempt.generation = 7u;
    fails += x64_storage_boot_attempt_from_handoff(&handoff, &attempt) == 0 &&
                     attempt.slot == 1u && attempt.generation == 7u
                 ? 0
                 : 1;
    handoff.slot_attempt.flags |= 0x80000000u;
    memset(&attempt, 0xA5, sizeof(attempt));
    fails += x64_storage_boot_attempt_from_handoff(&handoff, &attempt) != 0 &&
                     attempt.flags == 0u && attempt.generation == 0u
                 ? 0
                 : 1;
    handoff.slot_attempt.flags = BOOT_HANDOFF_SLOT_ATTEMPT_VALID;
    handoff.slot_attempt.generation = 0u;
    fails += x64_storage_boot_attempt_from_handoff(&handoff, &attempt) != 0
                 ? 0
                 : 1;
    handoff.version = 9u;
    handoff.slot_attempt.generation = 7u;
    fails += x64_storage_boot_attempt_from_handoff(&handoff, &attempt) != 0
                 ? 0
                 : 1;
  }

  {
    struct boot_handoff handoff = {0};
    struct capyos_gpt_identity identity = {0};
    handoff.version = 9u;
    handoff.efi_disk_last_lba_raw = 199u;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID;
    handoff.disk_identity.esp_lba_start = identity.esp.lba = 40u;
    handoff.disk_identity.esp_lba_count = identity.esp.sectors = 10u;
    handoff.disk_identity.boot_lba_start = identity.boot.lba = 50u;
    handoff.disk_identity.boot_lba_count = identity.boot.sectors = 20u;
    handoff.disk_identity.data_lba_start = identity.data.lba = 70u;
    handoff.disk_identity.data_lba_count = identity.data.sectors = 91u;
    identity.disk_sectors = 200u;
    handoff.disk_identity.disk_guid[0] = identity.disk_guid[0] = 1u;
    handoff.disk_identity.esp_partition_guid[0] = identity.esp.guid[0] = 2u;
    handoff.disk_identity.boot_partition_guid[0] = identity.boot.guid[0] = 3u;
    handoff.disk_identity.data_partition_guid[0] = identity.data.guid[0] = 4u;
    fails += x64_storage_boot_provider_identity_matches_handoff(
                 &handoff, &identity, 1)
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID |
                                  BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID;
    fails += !x64_storage_boot_provider_identity_matches_handoff(
                 &handoff, &identity, 1)
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID;
    fails += x64_storage_boot_provider_identity_matches_handoff(
                 &handoff, &identity, 0)
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID |
                                  0x80000000u;
    fails += !x64_storage_boot_provider_identity_matches_handoff(
                 &handoff, &identity, 0)
                 ? 0
                 : 1;
    handoff.disk_identity.flags = BOOT_HANDOFF_DISK_IDENTITY_VALID;
    identity.disk_sectors = 201u;
    fails += !x64_storage_boot_provider_identity_matches_handoff(
                 &handoff, &identity, 1)
                 ? 0
                 : 1;
    handoff.version = 8u;
    handoff.data_lba_start_raw = 70u;
    handoff.data_lba_count_raw = 91u;
  }

  {
    /* Stage planning is pure: it must pick the inactive slot from a snapshot
     * whose confirmed slot is the healthy running one, bound the payload by
     * that slot's capacity, and refuse anything it cannot justify. */
    struct boot_slot_snapshot snapshot;
    struct x64_storage_boot_stage_plan plan;
    uint8_t sha256[BOOT_SLOT_SHA256_SIZE];
    char oversized[BOOT_SLOT_VERSION_MAX + 8];
    memset(&snapshot, 0, sizeof(snapshot));
    memset(sha256, 0, sizeof(sha256));
    sha256[7] = 0x5Au;
    snapshot.version = BOOT_SLOT_SNAPSHOT_VERSION;
    snapshot.size = sizeof(snapshot);
    snapshot.generation = 4u;
    snapshot.authority_epoch = 2u;
    snapshot.lease_epoch = 3u;
    snapshot.manager.active_slot = 0u;
    snapshot.manager.confirmed_slot = 0u;
    snapshot.manager.pending_slot = BOOT_SLOT_NONE;
    snapshot.manager.slots[0].state = BOOT_SLOT_ACTIVE;
    snapshot.manager.slots[0].health_confirmed = 1;
    snapshot.manager.slots[0].payload_capacity_sectors = 100u;
    snapshot.manager.slots[1].state = BOOT_SLOT_EMPTY;
    snapshot.manager.slots[1].payload_capacity_sectors = 100u;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) == 0 &&
                     plan.slot == 1u && plan.image.payload_size == 700u &&
                     strcmp(plan.image.version, "9.9.9") == 0 &&
                     plan.image.payload_sha256[7] == 0x5Au
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 51200u,
                                                  sha256, &plan) == 0 &&
                     plan.image.payload_size == 51200u
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 51201u,
                                                  sha256, &plan) != 0 &&
                     plan.slot == 0u && plan.image.payload_size == 0u
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 0u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "", 700u, sha256,
                                                  &plan) != 0
                 ? 0
                 : 1;
    memset(oversized, 'v', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = '\0';
    fails += x64_storage_boot_provider_plan_stage(&snapshot, oversized, 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    memset(sha256, 0, sizeof(sha256));
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    sha256[7] = 0x5Au;
    /* Selecting the confirmed slot itself, or planning while an attempt is
     * still pending, would destroy the only known-good image. */
    snapshot.manager.slots[1].state = BOOT_SLOT_ACTIVE;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.manager.slots[1].state = BOOT_SLOT_FAILED;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) == 0
                 ? 0
                 : 1;
    snapshot.manager.pending_slot = 1u;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.manager.pending_slot = BOOT_SLOT_NONE;
    snapshot.manager.rollback_pending = 1;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.manager.rollback_pending = 0;
    snapshot.manager.slots[0].health_confirmed = 0;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.manager.slots[0].health_confirmed = 1;
    snapshot.generation = 0u;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.generation = 4u;
    snapshot.version = BOOT_SLOT_SNAPSHOT_VERSION + 1u;
    fails += x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9", 700u,
                                                  sha256, &plan) != 0
                 ? 0
                 : 1;
    snapshot.version = BOOT_SLOT_SNAPSHOT_VERSION;
    fails += x64_storage_boot_provider_plan_stage(NULL, "9.9.9", 700u, sha256,
                                                  &plan) != 0 &&
                     x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9",
                                                          700u, NULL,
                                                          &plan) != 0 &&
                     x64_storage_boot_provider_plan_stage(&snapshot, "9.9.9",
                                                          700u, sha256,
                                                          NULL) != 0
                 ? 0
                 : 1;
  }

  if (fails == 0)
    printf("[test_storage_boot_provider_policy] all passed\n");
  return fails;
}
