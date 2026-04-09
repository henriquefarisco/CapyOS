#include "arch/x86_64/hyperv_runtime_coordinator.h"

#include "arch/x86_64/storage_runtime.h"

static int coordinator_ops_ready(
    const struct x64_hyperv_runtime_coordinator_ops *ops) {
  return ops && ops->boot_services_active &&
         ops->allow_hybrid_storage_prepare &&
         ops->maybe_exit_boot_services_after_native_runtime &&
         ops->update_system_runtime_platform_status &&
         ops->print_input_runtime_status && ops->print_storage_runtime_status;
}

static int coordinator_promote_storage(
    const struct x64_hyperv_runtime_coordinator_ops *ops) {
  if (!coordinator_ops_ready(ops)) {
    return 0;
  }
  if (ops->boot_services_active()) {
    return 0;
  }
  if (x64_storage_runtime_try_enable_hyperv_native(
          ops->boot_services_active(),
          ops->allow_hybrid_storage_prepare(), ops->print) == 0) {
    return 0;
  }
  ops->update_system_runtime_platform_status();
  ops->print_storage_runtime_status();
  return 1;
}

static int coordinator_storage_ready(void) {
  struct storvsc_controller_status status;

  return x64_storage_runtime_hyperv_controller_status(&status) == 0 &&
         status.ready;
}

static void coordinator_bootstrap_storage_budget(
    const struct x64_hyperv_runtime_coordinator_ops *ops) {
  uint32_t round = 0u;
  uint32_t idle_rounds = 0u;

  if (!coordinator_ops_ready(ops) || ops->boot_services_active() ||
      !x64_storage_runtime_hyperv_present() || coordinator_storage_ready()) {
    return;
  }

  ops->print(
      "[storage] Budget StorVSC: seguindo sequencia estilo Linux (offer -> channel -> control -> ready) com fallback seguro caso o host nao responda a tempo.\n");
  for (round = 0u; round < 12u; ++round) {
    int rc = coordinator_promote_storage(ops);

    if (coordinator_storage_ready()) {
      ops->print("[storage] Budget StorVSC concluiu a promocao do controlador sintetico antes da rede.\n");
      return;
    }
    if (rc < 0) {
      ops->update_system_runtime_platform_status();
      ops->print(
          "[storage] Budget StorVSC encontrou erro; mantendo o fallback atual e seguindo o boot para evitar nova quebra.\n");
      ops->print_storage_runtime_status();
      return;
    }
    if (rc > 0) {
      idle_rounds = 0u;
      continue;
    }
    idle_rounds += 1u;
    if (idle_rounds >= 4u) {
      break;
    }
  }

  ops->update_system_runtime_platform_status();
  ops->print(
      "[storage] Budget StorVSC esgotado sem prontidao completa; mantendo o backend atual e continuando com telemetria detalhada.\n");
  ops->print_storage_runtime_status();
}

void x64_hyperv_runtime_after_native_ready(
    const struct x64_hyperv_runtime_coordinator_ops *ops) {
  if (!coordinator_ops_ready(ops)) {
    return;
  }

  ops->maybe_exit_boot_services_after_native_runtime();
  coordinator_bootstrap_storage_budget(ops);
}

int x64_hyperv_runtime_poll_promotions(
    struct x64_input_runtime *input_runtime,
    const struct x64_hyperv_runtime_coordinator_ops *ops) {
  int changed = 0;

  if (!input_runtime || !coordinator_ops_ready(ops)) {
    return 0;
  }

  if (!ops->boot_services_active()) {
    if (input_runtime->hyperv_deferred &&
        x64_input_try_enable_hyperv_native(input_runtime,
                                           ops->boot_services_active(),
                                           ops->print) != 0) {
      ops->update_system_runtime_platform_status();
      ops->print_input_runtime_status();
      changed = 1;
    }
  }

  if (coordinator_promote_storage(ops) != 0) {
    changed = 1;
  }

  return changed;
}
