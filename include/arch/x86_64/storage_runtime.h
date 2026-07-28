#ifndef ARCH_X86_64_STORAGE_RUNTIME_H
#define ARCH_X86_64_STORAGE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "boot/handoff.h"
#include "boot/boot_slot_block_provider.h"
#include "arch/x86_64/storage_boot_provider_policy.h"
#include "drivers/storage/efi_block.h"
#include "drivers/storage/storvsc_runtime.h"
#include "fs/block.h"

enum x64_storage_backend {
  X64_STORAGE_BACKEND_NONE = 0,
  X64_STORAGE_BACKEND_EFI_BLOCK_IO,
  X64_STORAGE_BACKEND_AHCI,
  X64_STORAGE_BACKEND_NVME,
  /* Append-only: ATA-PIO is a broad-hardware-compatibility fallback for
   * hypervisors that expose legacy IDE/ATA emulation (e.g. Hyper-V Gen1).
   * It never replaces NVMe/AHCI on the VMware + UEFI + E1000 track. */
  X64_STORAGE_BACKEND_ATA_PIO,
};

struct x64_storage_runtime_io {
  void (*print)(const char *message);
  void (*print_hex64)(uint64_t value);
  void (*print_dec_u32)(uint32_t value);
  void (*putc)(char ch);
};

struct block_device *x64_storage_runtime_open_handoff_data_device(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf);
const struct efi_block_device *x64_storage_runtime_active_efi(void);
const char *x64_storage_runtime_backend_name(void);
const char *x64_storage_runtime_data_path(void);
const char *x64_storage_runtime_native_candidate_name(void);
const char *x64_storage_runtime_native_data_path(void);
int x64_storage_runtime_uses_firmware(void);
int x64_storage_runtime_has_native_candidate(void);
int x64_storage_runtime_has_device(void);
struct block_device *x64_storage_runtime_raw_device(void);
int x64_storage_runtime_data_binding(uint32_t *out_lba,
                                     uint32_t *out_sectors);
int x64_storage_runtime_boot_provider_status(
    struct x64_storage_boot_provider_status *out);
#if 0
int x64_storage_runtime_stage_boot_payload(
    const char *version, const uint8_t *payload, size_t payload_len,
    uint64_t *out_generation);
#endif
int x64_storage_runtime_stage_boot_payload_sha256(
    const char *version, const uint8_t *payload, size_t payload_len,
    const uint8_t expected_sha256[BOOT_SLOT_SHA256_SIZE], uint32_t *out_slot,
    uint64_t *out_generation);
int x64_storage_runtime_arm_boot_slot(uint32_t slot,
                                     uint64_t expected_generation,
                                     uint64_t *out_generation);
int x64_storage_runtime_confirm_boot_health(
    const struct boot_slot_attempt_handoff *attempt);
int x64_storage_runtime_confirm_current_boot_health(void);
/* Durable rollback observation. Returns 2 when the running boot is itself the
 * loader-applied rollback, 1 when the metadata still carries a pending
 * rollback, 0 when nothing is pending and -1 when the persistent provider is
 * unavailable. */
int x64_storage_runtime_boot_rollback_check(
    const struct boot_slot_attempt_handoff *attempt);
int x64_storage_runtime_current_boot_rollback_check(void);
/* Copy of the attempt token the loader published for the running boot, already
 * validated by x64_storage_boot_attempt_from_handoff. Exposed so the boot log
 * can state which slot is running and whether it still owes a confirmation --
 * the loader's own message goes through the firmware console, which the official
 * VMware contract deliberately keeps off COM1. */
int x64_storage_runtime_current_boot_attempt(
    struct boot_slot_attempt_handoff *out);
int x64_storage_runtime_register_boot_provider(
    int persistent_mount_ready, const struct boot_slot_disk_binding *binding,
    struct boot_slot_block_provider *provider);
int x64_storage_runtime_hyperv_present(void);
int x64_storage_runtime_hyperv_bus_prepared(void);
int x64_storage_runtime_hyperv_bus_connected(void);
int x64_storage_runtime_hyperv_offer_cached(void);
const char *x64_storage_runtime_hyperv_phase_name(void);
const char *x64_storage_runtime_hyperv_gate_label(int boot_services_active);
const char *x64_storage_runtime_hyperv_next_action_label(
    int boot_services_active);
const char *x64_storage_runtime_hyperv_block_reason(int boot_services_active);
void x64_storage_runtime_allow_hyperv_hybrid_prepare(int allow);
int x64_storage_runtime_hyperv_controller_status(
    struct storvsc_controller_status *out);
uint32_t x64_storage_runtime_hyperv_attempt_count(void);
uint32_t x64_storage_runtime_hyperv_change_count(void);
int32_t x64_storage_runtime_hyperv_last_result(void);
const char *x64_storage_runtime_hyperv_last_action_label(int boot_services_active);
int x64_storage_runtime_try_prepare_hyperv_bus(void (*print)(const char *));
int x64_storage_runtime_manual_hyperv_step(int boot_services_active,
                                           void (*print)(const char *));
int x64_storage_runtime_try_enable_hyperv_native(
    int boot_services_active, int allow_hybrid_prepare,
    void (*print)(const char *));

#endif /* ARCH_X86_64_STORAGE_RUNTIME_H */
