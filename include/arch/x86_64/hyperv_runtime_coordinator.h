#ifndef ARCH_X86_64_HYPERV_RUNTIME_COORDINATOR_H
#define ARCH_X86_64_HYPERV_RUNTIME_COORDINATOR_H

#include "arch/x86_64/input_runtime.h"

struct x64_hyperv_runtime_coordinator_ops {
  int (*boot_services_active)(void);
  int (*allow_hybrid_storage_prepare)(void);
  void (*maybe_exit_boot_services_after_native_runtime)(void);
  void (*update_system_runtime_platform_status)(void);
  void (*print_input_runtime_status)(void);
  void (*print_storage_runtime_status)(void);
  void (*print)(const char *message);
};

void x64_hyperv_runtime_after_native_ready(
    const struct x64_hyperv_runtime_coordinator_ops *ops);
int x64_hyperv_runtime_poll_promotions(
    struct x64_input_runtime *input_runtime,
    const struct x64_hyperv_runtime_coordinator_ops *ops);

#endif /* ARCH_X86_64_HYPERV_RUNTIME_COORDINATOR_H */
