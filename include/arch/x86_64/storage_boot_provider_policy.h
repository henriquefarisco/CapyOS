#ifndef ARCH_X86_64_STORAGE_BOOT_PROVIDER_POLICY_H
#define ARCH_X86_64_STORAGE_BOOT_PROVIDER_POLICY_H

#include <stdint.h>

#include "boot/boot_slot_block_provider.h"
#include "boot/gpt_identity.h"
#include "boot/handoff.h"

enum x64_storage_boot_provider_reason {
  X64_STORAGE_BOOT_PROVIDER_READY = 0,
  X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT,
  X64_STORAGE_BOOT_PROVIDER_NO_RAW_DEVICE,
  X64_STORAGE_BOOT_PROVIDER_NO_DATA_BINDING,
  X64_STORAGE_BOOT_PROVIDER_NO_ESP_BINDING,
  X64_STORAGE_BOOT_PROVIDER_NO_FLUSH,
  X64_STORAGE_BOOT_PROVIDER_NO_CONTROL,
  X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN,
  X64_STORAGE_BOOT_PROVIDER_TOKEN_MISMATCH,
};

struct x64_storage_boot_provider_input {
  uint8_t persistent_mount_ready;
  uint8_t raw_device_ready;
  uint8_t data_binding_ready;
  uint8_t esp_binding_ready;
  uint8_t flush_ready;
};

struct x64_storage_boot_provider_status {
  uint8_t provider_ready;
  enum x64_storage_boot_provider_reason reason;
};

struct x64_storage_boot_provider_runtime_snapshot {
  uint8_t persistent_mount_ready;
  uint8_t active_native_backend;
  uint8_t storage_device_ready;
  uint8_t data_binding_verified;
  uint8_t esp_binding_verified;
  uint8_t flush_ready;
  uint8_t raw_read_ready;
  uint8_t raw_write_ready;
  uint32_t raw_block_size;
};

struct x64_storage_boot_stage_plan {
  uint32_t slot;
  struct boot_slot_image image;
};

void x64_storage_boot_provider_evaluate(
    const struct x64_storage_boot_provider_input *input,
    struct x64_storage_boot_provider_status *out);
const char *x64_storage_boot_provider_reason_label(
    enum x64_storage_boot_provider_reason reason);
int x64_storage_boot_provider_plan_stage(
    const struct boot_slot_snapshot *snapshot, const char *version,
    uint32_t payload_size, const uint8_t sha256[BOOT_SLOT_SHA256_SIZE],
    struct x64_storage_boot_stage_plan *out_plan);
void x64_storage_boot_provider_evaluate_runtime(
    const struct x64_storage_boot_provider_runtime_snapshot *snapshot,
    struct x64_storage_boot_provider_status *out);
int x64_storage_boot_provider_range_u32(uint64_t start, uint64_t count,
                                        uint32_t *out_start,
                                        uint32_t *out_count);
int x64_storage_boot_provider_select_data(
    int gpt_ready, uint32_t gpt_start, uint32_t gpt_count,
    int handoff_ready, uint32_t handoff_start, uint32_t handoff_count,
    uint32_t *out_start, uint32_t *out_count, int *out_verified);
int x64_storage_boot_provider_binding_from_handoff(
    const struct boot_handoff *handoff,
    struct boot_slot_disk_binding *out_binding);
int x64_storage_boot_attempt_from_handoff(
    const struct boot_handoff *handoff,
    struct boot_slot_attempt_handoff *out_attempt);
int x64_storage_boot_provider_identity_matches_handoff(
    const struct boot_handoff *handoff,
    const struct capyos_gpt_identity *identity, int identity_strict);

#endif /* ARCH_X86_64_STORAGE_BOOT_PROVIDER_POLICY_H */
