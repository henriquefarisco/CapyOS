#include "arch/x86_64/native_runtime_gate.h"

#include "arch/x86_64/storage_runtime.h"

static int handoff_has_runtime_flag(const struct boot_handoff *handoff,
                                    uint32_t flag) {
  return handoff && (handoff->runtime_flags & flag) != 0u;
}

static int handoff_uses_legacy_runtime_contract(
    const struct boot_handoff *handoff) {
  return handoff && handoff->version < 5;
}

static int handoff_boot_services_active(const struct boot_handoff *handoff) {
  if (!handoff || handoff->efi_system_table == 0) {
    return 0;
  }
  if (handoff_has_runtime_flag(handoff,
                               BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE)) {
    return 1;
  }
  return handoff_uses_legacy_runtime_contract(handoff);
}

static int handoff_has_exit_boot_services_contract(
    const struct boot_handoff *handoff) {
  return handoff && handoff->version >= 6 && handoff->efi_system_table != 0 &&
         handoff->efi_image_handle != 0 && handoff->efi_map_key != 0 &&
         handoff->memmap != 0 && handoff->memmap_size != 0 &&
         handoff->memmap_capacity != 0;
}

static int input_ready_for_native_runtime(
    const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return 0;
  }
  if (x64_input_has_native_backend(runtime)) {
    return 1;
  }
  /* On Hyper-V Gen2 there is no PS/2 and the VMBus keyboard is deferred
   * until Boot Services exit.  Accept deferred Hyper-V input as sufficient:
   * the keyboard will be promoted immediately after EBS via the coordinator,
   * and EFI ConIn bridges input during the brief transition window. */
  if (runtime->hyperv_deferred) {
    return 1;
  }
  return 0;
}

static int storage_ready_for_native_runtime(void) {
  if (!x64_storage_runtime_has_device()) {
    return 0;
  }
  return x64_storage_runtime_uses_firmware() ? 0 : 1;
}

void x64_native_runtime_gate_eval(const struct boot_handoff *handoff,
                                  const struct x64_input_runtime *runtime,
                                  int exit_attempted, int exit_done,
                                  uint64_t exit_status,
                                  struct x64_native_runtime_gate_status *out) {
  if (!out) {
    return;
  }

  out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_UNKNOWN;
  out->last_status = exit_status;

  if (exit_done || !handoff_boot_services_active(handoff)) {
    out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_NATIVE;
    return;
  }
  if (exit_attempted) {
    out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_FAILED;
    return;
  }
  if (!handoff_has_exit_boot_services_contract(handoff)) {
    out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_CONTRACT;
    return;
  }
  if (!input_ready_for_native_runtime(runtime)) {
    out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT;
    return;
  }
  /* On Hyper-V Gen2 all storage is synthetic (StorVSC) and depends on VMBus
   * which in turn depends on ExitBootServices.  When the keyboard is deferred
   * we know this is a Gen2 VM: skip storage checks because StorVSC will be
   * promoted by the coordinator immediately after EBS completes.  The kernel
   * is already in memory and the system can use ramdisk fallback until
   * StorVSC is ready. */
  if (runtime && !runtime->hyperv_deferred) {
    if (!x64_storage_runtime_has_device()) {
      out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_DEVICE;
      return;
    }
    if (!storage_ready_for_native_runtime()) {
      out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE;
      return;
    }
  }

  out->gate = SYSTEM_EXIT_BOOT_SERVICES_GATE_READY;
}

int x64_native_runtime_gate_is_ready(
    const struct x64_native_runtime_gate_status *status) {
  return status && status->gate == SYSTEM_EXIT_BOOT_SERVICES_GATE_READY;
}
