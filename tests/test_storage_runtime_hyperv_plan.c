#include <stdio.h>

#include "arch/x86_64/storage_runtime_hyperv_plan.h"
#include "drivers/storage/storvsc_runtime.h"

static int expect_plan(const char *name, uint8_t expected_gate,
                       uint8_t expected_action, uint8_t present,
                       uint8_t configured, uint8_t enabled, uint8_t phase,
                       uint8_t bus_prepared, uint8_t bus_connected,
                       uint8_t offer_cached, uint8_t hybrid_prepare_allowed,
                       int boot_services_active, int uses_firmware) {
  struct x64_storage_hyperv_plan plan;

  x64_storage_hyperv_plan_build(present, configured, enabled, phase,
                                bus_prepared, bus_connected, offer_cached,
                                hybrid_prepare_allowed, boot_services_active,
                                uses_firmware, &plan);
  if (plan.gate_state != expected_gate || plan.next_action != expected_action) {
    printf("[storage_hyperv_plan] %s expected gate=%u action=%u got gate=%u action=%u\n",
           name, (unsigned)expected_gate, (unsigned)expected_action,
           (unsigned)plan.gate_state, (unsigned)plan.next_action);
    return 1;
  }
  return 0;
}

int run_storage_runtime_hyperv_plan_tests(void) {
  int fails = 0;

  fails += expect_plan("invalid", X64_STORAGE_HYPERV_GATE_INVALID,
                       X64_STORAGE_HYPERV_ACTION_INVALID, 0, 0, 0, 0, 0, 0, 0,
                       0, 0, 0);
  fails += expect_plan("wait-runtime", X64_STORAGE_HYPERV_GATE_WAIT_RUNTIME,
                       X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME, 1, 0, 0, 0, 0,
                       0, 0, 0, 0, 0);
  fails += expect_plan("wait-platform", X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM,
                       X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM, 1, 1, 0, 0, 0,
                       0, 0, 0, 1, 0);
  fails += expect_plan("hybrid-prepare-bus",
                       X64_STORAGE_HYPERV_GATE_PREPARE_BUS,
                       X64_STORAGE_HYPERV_ACTION_PREPARE_BUS, 1, 1, 0, 0, 0,
                       0, 0, 1, 1, 1);
  fails += expect_plan("connected-hybrid-wait-platform",
                       X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM,
                       X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM, 1, 1, 0, 0, 1,
                       1, 0, 0, 1, 1);
  fails += expect_plan("prepare-bus", X64_STORAGE_HYPERV_GATE_PREPARE_BUS,
                       X64_STORAGE_HYPERV_ACTION_PREPARE_BUS, 1, 1, 0, 0, 0,
                       0, 0, 0, 0, 0);
  fails += expect_plan("wait-bus", X64_STORAGE_HYPERV_GATE_WAIT_BUS,
                       X64_STORAGE_HYPERV_ACTION_WAIT_BUS, 1, 1, 0, 0, 1, 0,
                       0, 0, 0, 0);
  fails += expect_plan("wait-offer", X64_STORAGE_HYPERV_GATE_WAIT_OFFER,
                       X64_STORAGE_HYPERV_ACTION_WAIT_OFFER, 1, 1, 0, 0, 1, 1,
                       0, 0, 0, 0);
  fails += expect_plan("enable-probe", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE, 1, 1, 0, 0, 1,
                       1, 1, 0, 0, 0);
  fails += expect_plan("step-probe", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_STEP_PROBE, 1, 1, 1,
                       STORVSC_RUNTIME_PROBE, 1, 1, 1, 0, 0, 0);
  fails += expect_plan("step-runtime-channel", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME, 1, 1, 1,
                       STORVSC_RUNTIME_CHANNEL, 1, 1, 1, 0, 0, 0);
  fails += expect_plan("step-runtime-control", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME, 1, 1, 1,
                       STORVSC_RUNTIME_CONTROL, 1, 1, 1, 0, 0, 0);
  fails += expect_plan("noop-ready", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_NOOP, 1, 1, 1,
                       STORVSC_RUNTIME_READY, 1, 1, 1, 0, 0, 0);
  fails += expect_plan("noop-failed", X64_STORAGE_HYPERV_GATE_OPEN,
                       X64_STORAGE_HYPERV_ACTION_NOOP, 1, 1, 1,
                       STORVSC_RUNTIME_FAILED, 1, 1, 1, 0, 0, 0);

  if (fails == 0) {
    printf("[tests] storage_runtime_hyperv_plan OK\n");
  }
  return fails;
}
