#ifndef X64_STORAGE_RUNTIME_HYPERV_H
#define X64_STORAGE_RUNTIME_HYPERV_H

#include "drivers/storage/storvsc_runtime.h"

struct x64_storage_hyperv_runtime_state {
  int present;
  int hybrid_prepare_allowed;
  struct storvsc_runtime_state runtime;
  int wait_bus_logged;
  int wait_offer_logged;
  int wait_runtime_logged;
  int enabled_logged;
  int probe_logged;
  int prepare_logged;
  int ready_logged;
  int fallback_logged;
  int cooldown_logged;
  uint8_t last_action;
  int32_t last_result;
  int32_t last_failure_code;
  uint32_t action_attempts;
  uint32_t action_changes;
  uint32_t cooldown_remaining;
  uint32_t failure_streak;
};

void x64_storage_hyperv_runtime_reset(
    struct x64_storage_hyperv_runtime_state *state);
int x64_storage_hyperv_runtime_present(
    const struct x64_storage_hyperv_runtime_state *state);
int x64_storage_hyperv_runtime_bus_prepared(
    const struct x64_storage_hyperv_runtime_state *state);
int x64_storage_hyperv_runtime_bus_connected(
    const struct x64_storage_hyperv_runtime_state *state);
int x64_storage_hyperv_runtime_offer_cached(
    const struct x64_storage_hyperv_runtime_state *state);
const char *x64_storage_hyperv_runtime_phase_name(
    const struct x64_storage_hyperv_runtime_state *state);
uint8_t x64_storage_hyperv_runtime_gate_state(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare);
uint8_t x64_storage_hyperv_runtime_next_action(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare);
const char *x64_storage_hyperv_runtime_block_reason(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare);
void x64_storage_hyperv_runtime_allow_hybrid_prepare(
    struct x64_storage_hyperv_runtime_state *state, int allow);
int x64_storage_hyperv_runtime_controller_status(
    const struct x64_storage_hyperv_runtime_state *state,
    struct storvsc_controller_status *out);
int x64_storage_hyperv_runtime_try_prepare_bus(
    struct x64_storage_hyperv_runtime_state *state,
    void (*print)(const char *));
int x64_storage_hyperv_runtime_try_enable_native(
    struct x64_storage_hyperv_runtime_state *state, int boot_services_active,
    int uses_firmware, int allow_hybrid_prepare, void (*print)(const char *));
int x64_storage_hyperv_runtime_manual_step(
    struct x64_storage_hyperv_runtime_state *state, int boot_services_active,
    int uses_firmware, void (*print)(const char *));
uint32_t x64_storage_hyperv_runtime_attempt_count(
    const struct x64_storage_hyperv_runtime_state *state);
uint32_t x64_storage_hyperv_runtime_change_count(
    const struct x64_storage_hyperv_runtime_state *state);
int32_t x64_storage_hyperv_runtime_last_result(
    const struct x64_storage_hyperv_runtime_state *state);
uint8_t x64_storage_hyperv_runtime_last_action(
    const struct x64_storage_hyperv_runtime_state *state);

#endif /* X64_STORAGE_RUNTIME_HYPERV_H */
