#ifndef ARCH_X86_64_STORAGE_RUNTIME_H
#define ARCH_X86_64_STORAGE_RUNTIME_H

#include <stdint.h>

#include "boot/handoff.h"
#include "drivers/storage/efi_block.h"
#include "drivers/storage/storvsc_runtime.h"
#include "fs/block.h"

enum x64_storage_backend {
  X64_STORAGE_BACKEND_NONE = 0,
  X64_STORAGE_BACKEND_EFI_BLOCK_IO,
  X64_STORAGE_BACKEND_AHCI,
  X64_STORAGE_BACKEND_NVME,
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
