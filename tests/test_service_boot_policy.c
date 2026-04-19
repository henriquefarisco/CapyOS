#include <stdio.h>

#include "services/service_boot_policy.h"
#include "services/service_manager.h"

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "[service_boot_policy] %s\n", msg);
    return 1;
  }
  return 0;
}

int run_service_boot_policy_tests(void) {
  int fails = 0;
  struct system_service_boot_policy_input input;
  struct system_service_boot_policy_decision decision;

  input.requested_target = SYSTEM_SERVICE_TARGET_NETWORK;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 1u;
  input.validated_network_supported = 1u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.bootstrap_target == SYSTEM_SERVICE_TARGET_CORE,
                       "bootstrap target should start in core");
  fails += expect_true(decision.final_target == SYSTEM_SERVICE_TARGET_NETWORK,
                       "network target should be preserved when dependencies are healthy");
  fails += expect_true(decision.degraded == 0u,
                       "healthy requested target should not degrade");

  input.requested_target = SYSTEM_SERVICE_TARGET_FULL;
  input.shell_runtime_ready = 0u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 1u;
  input.validated_network_supported = 1u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.final_target ==
                           SYSTEM_SERVICE_TARGET_MAINTENANCE,
                       "missing shell runtime should force maintenance");
  fails += expect_true(decision.forced_maintenance == 1u &&
                           decision.reason ==
                               SYSTEM_SERVICE_BOOT_POLICY_STORAGE_RUNTIME_UNAVAILABLE,
                       "storage runtime failure reason mismatch");

  input.requested_target = SYSTEM_SERVICE_TARGET_FULL;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 0u;
  input.network_status_available = 1u;
  input.validated_network_supported = 1u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.final_target ==
                           SYSTEM_SERVICE_TARGET_MAINTENANCE,
                       "missing validated storage should force maintenance");
  fails += expect_true(
      decision.reason ==
          SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_STORAGE_UNAVAILABLE,
      "validated storage failure reason mismatch");

  input.requested_target = SYSTEM_SERVICE_TARGET_NETWORK;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 0u;
  input.validated_network_supported = 1u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.final_target == SYSTEM_SERVICE_TARGET_CORE,
                       "missing network status should degrade to core");
  fails += expect_true(decision.forced_core == 1u &&
                           decision.reason ==
                               SYSTEM_SERVICE_BOOT_POLICY_NETWORK_STATUS_UNAVAILABLE,
                       "network status degradation reason mismatch");

  input.requested_target = SYSTEM_SERVICE_TARGET_FULL;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 1u;
  input.validated_network_supported = 0u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(
      decision.final_target == SYSTEM_SERVICE_TARGET_CORE,
      "unsupported validated network runtime should degrade full target to core");
  fails += expect_true(
      decision.reason ==
          SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE,
      "validated network failure reason mismatch");

  input.requested_target = SYSTEM_SERVICE_TARGET_CORE;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 0u;
  input.validated_network_supported = 0u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.final_target == SYSTEM_SERVICE_TARGET_CORE,
                       "core target should not downgrade because network is absent");
  fails += expect_true(decision.degraded == 0u,
                       "core target should remain stable without network");

  input.requested_target = 999u;
  input.shell_runtime_ready = 1u;
  input.validated_storage_ready = 1u;
  input.network_status_available = 1u;
  input.validated_network_supported = 1u;
  service_boot_policy_evaluate(&input, &decision);
  fails += expect_true(decision.requested_target ==
                           SYSTEM_SERVICE_TARGET_NETWORK,
                       "invalid requested target should normalize to network");
  fails += expect_true(
      service_boot_policy_reason_label(
          SYSTEM_SERVICE_BOOT_POLICY_VALIDATED_NETWORK_UNAVAILABLE)[0] != '\0',
      "boot policy reason labels should be available");

  if (fails == 0) {
    printf("[tests] service_boot_policy OK\n");
  }
  return fails;
}
