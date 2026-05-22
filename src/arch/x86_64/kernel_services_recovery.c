/* kernel_services_recovery.c — Boot policy, maintenance + recovery surface.
 *
 * Split from kernel_services.c to keep each TU ≤ 900 lines.
 * Owns:
 *   - kernel_service_target_from_settings() / kernel_log_boot_policy_decision().
 *   - kernel_boots_in_maintenance_mode() + kernel_boot_maintenance_reason().
 *   - x64_kernel_recovery_status_get() + summary/resume/request hooks.
 *   - kernel_start_maintenance_session() — recovery-user session factory.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/storage_runtime.h"
#include "auth/session.h"
#include "auth/user.h"
#include "core/system_init.h"
#include "kernel/log/klog.h"
#include "lang/localization.h"
#include "net/stack.h"
#include "services/service_boot_policy.h"
#include "services/service_manager.h"
#include "services/update_agent.h"

/* ── boot policy helpers ─────────────────────────────────────────────── */

uint32_t kernel_service_target_from_settings(
    const struct system_settings *settings) {
  struct system_service_target_status target;
  if (settings &&
      service_manager_target_find(settings->service_target, &target) == 0) {
    return target.id;
  }
  return SYSTEM_SERVICE_TARGET_NETWORK;
}

void kernel_log_boot_policy_decision(
    const struct system_service_boot_policy_decision *decision) {
  if (!decision || !decision->degraded) {
    return;
  }
  if (decision->forced_maintenance) {
    klog(KLOG_WARN,
         "[services] Boot policy forced maintenance target for this boot.");
  } else if (decision->forced_core) {
    klog(KLOG_WARN, "[services] Boot policy downgraded target to core.");
  }
  klog(KLOG_WARN, service_boot_policy_reason_summary(decision->reason));
}

int kernel_boots_in_maintenance_mode(void) {
  return g_boot_policy_decision.final_target ==
         SYSTEM_SERVICE_TARGET_MAINTENANCE;
}

const char *kernel_boot_maintenance_reason(void) {
  if (!kernel_boots_in_maintenance_mode()) {
    return NULL;
  }
  if (!g_boot_policy_decision.degraded) {
    return "Maintenance target requested for this boot";
  }
  return service_boot_policy_reason_summary(g_boot_policy_decision.reason);
}

/* ── x64_kernel_recovery_status_get ──────────────────────────────────── */

void x64_kernel_recovery_status_get(struct x64_kernel_recovery_status *out) {
  struct system_service_target_status active_target;
  struct system_update_status update_status;

  if (!out) {
    return;
  }
  update_agent_status_get(&update_status);
  out->maintenance_session = g_runtime_maintenance_mode ? 1u : 0u;
  out->degraded = g_boot_policy_decision.degraded;
  out->forced_maintenance = g_boot_policy_decision.forced_maintenance;
  out->forced_core = g_boot_policy_decision.forced_core;
  out->shell_fs_ready = g_shell_fs_ready ? 1u : 0u;
  out->persistent_storage = g_shell_persistent_storage ? 1u : 0u;
  out->recovery_ram_fallback = g_shell_recovery_ram_fallback ? 1u : 0u;
  out->reason = g_boot_policy_decision.reason;
  out->journal_recovery_cause = capyfs_journal_last_recovery_cause();
  out->bootstrap_target = g_boot_policy_decision.bootstrap_target;
  out->requested_target = g_boot_policy_decision.requested_target;
  out->boot_target = g_boot_policy_decision.final_target;
  out->active_target = g_boot_policy_decision.final_target;
  out->update_catalog_present = update_status.catalog_present;
  out->update_available = update_status.update_available;
  out->update_stage_ready = update_status.stage_ready;
  out->update_pending_activation = update_status.pending_activation;
  out->update_last_result = update_status.last_result;
  local_copy(out->update_channel, sizeof(out->update_channel),
             update_status.channel);
  local_copy(out->update_branch, sizeof(out->update_branch),
             update_status.branch);
  local_copy(out->update_available_version,
             sizeof(out->update_available_version),
             update_status.available_version);
  local_copy(out->update_staged_version, sizeof(out->update_staged_version),
             update_status.staged_version);
  if (service_manager_target_current(&active_target) == 0) {
    out->active_target = active_target.id;
  }
}

const char *x64_kernel_recovery_reason_summary(void) {
  if (g_boot_policy_decision.degraded) {
    return service_boot_policy_reason_summary(g_boot_policy_decision.reason);
  }
  if (kernel_boots_in_maintenance_mode()) {
    return "Maintenance target requested for this boot";
  }
  return "Boot policy preserved the requested target";
}

int x64_kernel_recovery_resume_target(uint32_t target_id) {
  struct net_stack_status net_status;

  if (target_id >= SYSTEM_SERVICE_TARGET_COUNT) {
    return -1;
  }
  if (target_id != SYSTEM_SERVICE_TARGET_MAINTENANCE &&
      (g_shell_recovery_ram_fallback || !g_shell_persistent_storage ||
       !x64_storage_runtime_has_device())) {
    return -2;
  }
  if (target_id == SYSTEM_SERVICE_TARGET_NETWORK ||
      target_id == SYSTEM_SERVICE_TARGET_FULL) {
    if (net_stack_status(&net_status) != 0) {
      return -3;
    }
    if (!net_status.runtime_supported) {
      return -4;
    }
  }
  if (service_manager_target_apply(target_id) < 0) {
    return -5;
  }
  kernel_persist_recovery_artifacts("resume-target");
  kernel_update_recovery_snapshot_work(0);
  return 0;
}

int x64_kernel_recovery_request_normal_login(uint32_t target_id) {
  int rc = 0;

  if (!g_runtime_maintenance_mode) {
    return -6;
  }
  if (target_id == SYSTEM_SERVICE_TARGET_MAINTENANCE) {
    return -7;
  }
  rc = x64_kernel_recovery_resume_target(target_id);
  if (rc != 0) {
    return rc;
  }
  g_runtime_maintenance_mode = 0;
  g_recovery_login_requested = 1;
  kernel_persist_recovery_artifacts("leave-maintenance");
  kernel_update_recovery_snapshot_work(0);
  return 0;
}

int x64_kernel_recovery_maintenance_active(void) {
  return g_runtime_maintenance_mode ? 1 : 0;
}

int kernel_start_maintenance_session(struct session_context *session,
                                     const struct system_settings *settings) {
  struct user_record recovery_user;
  const char *default_language = "en";

  if (!session) {
    return -1;
  }
  if (settings && settings->language[0]) {
    const char *normalized = localization_normalize_language(settings->language);
    if (normalized) {
      default_language = normalized;
    }
  }

  user_record_clear(&recovery_user);
  local_copy(recovery_user.username, sizeof(recovery_user.username),
             "maintenance");
  local_copy(recovery_user.role, sizeof(recovery_user.role), "recovery");
  local_copy(recovery_user.home, sizeof(recovery_user.home), "/system");
  recovery_user.uid = 0u;
  recovery_user.gid = 0u;

  return session_begin(session, &recovery_user, default_language);
}
