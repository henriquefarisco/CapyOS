#include <stdio.h>
#include <string.h>

#include "arch/x86_64/native_runtime_gate.h"
#include "drivers/storage/storvsc_runtime.h"

static int g_input_ready = 0;
static int g_storage_device = 0;
static int g_storage_firmware = 0;
static int g_storage_synth_ready = 0;

int x64_input_has_native_backend(const struct x64_input_runtime *runtime) {
  (void)runtime;
  return g_input_ready;
}

int x64_storage_runtime_has_device(void) { return g_storage_device; }

int x64_storage_runtime_uses_firmware(void) { return g_storage_firmware; }

int x64_storage_runtime_hyperv_controller_status(
    struct storvsc_controller_status *out) {
  memset(out, 0, sizeof(*out));
  out->ready = g_storage_synth_ready ? 1u : 0u;
  return 0;
}

static void reset_fixtures(void) {
  g_input_ready = 0;
  g_storage_device = 0;
  g_storage_firmware = 0;
  g_storage_synth_ready = 0;
}

static void prepare_handoff(struct boot_handoff *handoff) {
  memset(handoff, 0, sizeof(*handoff));
  handoff->version = BOOT_HANDOFF_VERSION;
  handoff->efi_system_table = 1u;
  handoff->efi_image_handle = 2u;
  handoff->efi_map_key = 3u;
  handoff->memmap = 4u;
  handoff->memmap_size = 5u;
  handoff->memmap_capacity = 6u;
  handoff->runtime_flags = BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE |
                           BOOT_HANDOFF_RUNTIME_HYBRID_BOOT;
}

static int expect_gate(const char *name, uint8_t expected_gate,
                       uint64_t expected_status,
                       const struct boot_handoff *handoff,
                       int exit_attempted, int exit_done, uint64_t exit_status,
                       int input_prepared) {
  struct x64_native_runtime_gate_status status;
  struct x64_input_runtime runtime;

  memset(&runtime, 0, sizeof(runtime));
  runtime.hyperv_transport_prepared = input_prepared ? 1 : 0;
  memset(&status, 0, sizeof(status));
  x64_native_runtime_gate_eval(handoff, &runtime, exit_attempted, exit_done,
                               exit_status, &status);
  if (status.gate != expected_gate || status.last_status != expected_status) {
    printf("[native_runtime_gate] %s expected gate=%u status=%llu got gate=%u status=%llu\n",
           name, (unsigned)expected_gate,
           (unsigned long long)expected_status, (unsigned)status.gate,
           (unsigned long long)status.last_status);
    return 1;
  }
  return 0;
}

static int expect_gate_deferred(const char *name, uint8_t expected_gate,
                                uint64_t expected_status,
                                const struct boot_handoff *handoff,
                                int exit_attempted, int exit_done,
                                uint64_t exit_status) {
  struct x64_native_runtime_gate_status status;
  struct x64_input_runtime runtime;

  memset(&runtime, 0, sizeof(runtime));
  runtime.hyperv_deferred = 1;
  memset(&status, 0, sizeof(status));
  x64_native_runtime_gate_eval(handoff, &runtime, exit_attempted, exit_done,
                               exit_status, &status);
  if (status.gate != expected_gate || status.last_status != expected_status) {
    printf("[native_runtime_gate] %s expected gate=%u status=%llu got gate=%u status=%llu\n",
           name, (unsigned)expected_gate,
           (unsigned long long)expected_status, (unsigned)status.gate,
           (unsigned long long)status.last_status);
    return 1;
  }
  return 0;
}

int run_native_runtime_gate_tests(void) {
  int fails = 0;
  struct boot_handoff handoff;

  prepare_handoff(&handoff);

  reset_fixtures();
  handoff.runtime_flags = 0u;
  fails += expect_gate("native", SYSTEM_EXIT_BOOT_SERVICES_GATE_NATIVE, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  handoff.version = 5u;
  handoff.efi_map_key = 0u;
  fails += expect_gate("wait-contract",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_CONTRACT, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  fails += expect_gate("wait-input", SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT,
                       0u, &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  g_input_ready = 1;
  fails += expect_gate("wait-storage-device",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_DEVICE, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  reset_fixtures();
  fails += expect_gate("prepared-input-still-wait-input",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT, 0u, &handoff,
                       0, 0, 0u, 1);

  prepare_handoff(&handoff);
  g_input_ready = 1;
  g_storage_device = 1;
  g_storage_firmware = 1;
  fails += expect_gate("wait-storage-firmware",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  g_input_ready = 1;
  g_storage_device = 1;
  g_storage_firmware = 0;
  fails += expect_gate("ready", SYSTEM_EXIT_BOOT_SERVICES_GATE_READY, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  g_input_ready = 1;
  g_storage_device = 1;
  g_storage_firmware = 1;
  g_storage_synth_ready = 1;
  fails += expect_gate("wait-storage-firmware-with-synthetic-storage",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE, 0u,
                       &handoff, 0, 0, 0u, 0);

  prepare_handoff(&handoff);
  reset_fixtures();
  g_storage_device = 1;
  g_storage_firmware = 0;
  fails += expect_gate("prepared-input-still-wait-input-even-with-storage",
                       SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT, 0u, &handoff,
                       0, 0, 0u, 1);

  prepare_handoff(&handoff);
  g_input_ready = 1;
  g_storage_device = 1;
  g_storage_firmware = 0;
  fails += expect_gate("failed", SYSTEM_EXIT_BOOT_SERVICES_GATE_FAILED, 7u,
                       &handoff, 1, 0, 7u, 0);

  /* Hyper-V Gen2 deferred input: hyperv_deferred alone should pass both
   * the input gate and the storage gate (both are synthetic on Gen2),
   * reaching READY directly. */
  prepare_handoff(&handoff);
  reset_fixtures();
  fails += expect_gate_deferred("deferred-hyperv-skips-to-ready",
                                SYSTEM_EXIT_BOOT_SERVICES_GATE_READY,
                                0u, &handoff, 0, 0, 0u);

  /* Deferred input + storage already present = still READY. */
  prepare_handoff(&handoff);
  reset_fixtures();
  g_storage_device = 1;
  g_storage_firmware = 0;
  fails += expect_gate_deferred("deferred-hyperv-with-storage-ready",
                                SYSTEM_EXIT_BOOT_SERVICES_GATE_READY,
                                0u, &handoff, 0, 0, 0u);

  if (fails == 0) {
    printf("[tests] native_runtime_gate OK\n");
  }
  return fails;
}
