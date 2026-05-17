#include <stdio.h>
#include <string.h>

#include "arch/x86_64/hyperv_input_gate.h"

static int expect_gate(const char *name, uint8_t expected,
                       const struct x64_input_runtime *runtime,
                       int boot_services_active) {
  uint8_t actual = x64_hyperv_input_gate_state(runtime, boot_services_active);
  if (actual != expected) {
    printf("[input_hyperv_gate] %s expected %u got %u\n", name,
           (unsigned)expected, (unsigned)actual);
    return 1;
  }
  return 0;
}

int run_input_hyperv_gate_tests(void) {
  int fails = 0;
  struct x64_input_runtime runtime;

  memset(&runtime, 0, sizeof(runtime));
  fails += expect_gate("off", SYSTEM_HYPERV_INPUT_GATE_OFF, &runtime, 1);

  runtime.has_hyperv = 1;
  fails += expect_gate("active", SYSTEM_HYPERV_INPUT_GATE_ACTIVE, &runtime, 1);

  memset(&runtime, 0, sizeof(runtime));
  runtime.hyperv_deferred = 1;
  fails += expect_gate("wait-boot-services",
                       SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES, &runtime,
                       1);

  runtime.hyperv_transport_prepared = 1;
  fails += expect_gate("prepared-still-wait-boot-services",
                       SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES, &runtime,
                       1);
  runtime.hyperv_transport_prepared = 0;

  fails += expect_gate("ready", SYSTEM_HYPERV_INPUT_GATE_READY, &runtime, 0);

  runtime.hyperv_promotion_attempted = 1;
  runtime.hyperv_promotion_attempts = 1;
  fails += expect_gate("retry", SYSTEM_HYPERV_INPUT_GATE_RETRY, &runtime, 0);

  runtime.hyperv_promotion_attempts = 3;
  fails += expect_gate("failed", SYSTEM_HYPERV_INPUT_GATE_FAILED, &runtime, 0);

  if (fails == 0) {
    printf("[tests] input_hyperv_gate OK\n");
  }
  return fails;
}
