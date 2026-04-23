#ifndef ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H
#define ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H

#include "services/update_agent.h"

#include <stdint.h>

struct x64_kernel_recovery_status {
  uint8_t maintenance_session;
  uint8_t degraded;
  uint8_t forced_maintenance;
  uint8_t forced_core;
  uint8_t shell_fs_ready;
  uint8_t persistent_storage;
  uint8_t recovery_ram_fallback;
  uint8_t reason;
  uint32_t bootstrap_target;
  uint32_t requested_target;
  uint32_t boot_target;
  uint32_t active_target;
  uint8_t update_catalog_present;
  uint8_t update_available;
  uint8_t update_stage_ready;
  uint8_t update_pending_activation;
  int32_t update_last_result;
  char update_channel[UPDATE_AGENT_CHANNEL_MAX];
  char update_branch[UPDATE_AGENT_BRANCH_MAX];
  char update_available_version[UPDATE_AGENT_VERSION_MAX];
  char update_staged_version[UPDATE_AGENT_VERSION_MAX];
};

void x64_kernel_recovery_status_get(struct x64_kernel_recovery_status *out);
const char *x64_kernel_recovery_reason_summary(void);
int x64_kernel_recovery_resume_target(uint32_t target_id);
int x64_kernel_recovery_request_normal_login(uint32_t target_id);
int x64_kernel_recovery_maintenance_active(void);

int x64_kernel_manual_prepare_hyperv_input(void);
int x64_kernel_manual_prepare_hyperv_storage(void);
int x64_kernel_manual_prepare_native_bridge(void);
int x64_kernel_manual_prepare_hyperv_synic(void);
int x64_kernel_manual_try_exit_boot_services(void);
int x64_kernel_manual_native_runtime_step(void);
void x64_kernel_runtime_poll_background(void);

#endif /* ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H */
