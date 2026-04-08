#ifndef ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H
#define ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H

#include <stdint.h>

struct x64_kernel_recovery_status {
  uint8_t maintenance_session;
  uint8_t degraded;
  uint8_t forced_maintenance;
  uint8_t forced_core;
  uint8_t shell_fs_ready;
  uint8_t persistent_storage;
  uint8_t reason;
  uint32_t bootstrap_target;
  uint32_t requested_target;
  uint32_t boot_target;
  uint32_t active_target;
};

void x64_kernel_recovery_status_get(struct x64_kernel_recovery_status *out);
const char *x64_kernel_recovery_reason_summary(void);
int x64_kernel_recovery_resume_target(uint32_t target_id);

int x64_kernel_manual_prepare_hyperv_input(void);
int x64_kernel_manual_prepare_hyperv_storage(void);
int x64_kernel_manual_prepare_native_bridge(void);
int x64_kernel_manual_prepare_hyperv_synic(void);
int x64_kernel_manual_try_exit_boot_services(void);
int x64_kernel_manual_native_runtime_step(void);

#endif /* ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H */
