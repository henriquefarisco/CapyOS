/*
 * src/auth/login_runtime/contract_policy.c
 *
 * Contract/policy resolution for the login runtime — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.1 of
 * the Estagio C dedicated plan.  Hosts three public functions:
 *
 *   - login_window_contract_evaluate
 *   - login_recovery_resume_policy_evaluate
 *   - login_window_credential_policy_from_contract
 *
 * Each function reads only from the supplied `login_runtime_ops`
 * vtable and the `out` parameters; no file-level static state is
 * introduced. The shared `ops_ready` helper lives in
 * `auth/internal/login_runtime_internal.h` (PR C.0).
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_contract_evaluate(const struct login_runtime_ops *ops,
                                   struct login_window_contract *out) {
  if (!out) return -1;
  out->ready = 0;
  out->has_input = 0;
  out->maintenance_mode = 0;
  out->session_available = 0;
  out->settings_available = 0;
  out->recovery_available = 0;
  out->shell_callbacks_ready = 0;
  out->auth_callbacks_ready = 0;
  out->ui_callbacks_ready = 0;
  out->blocked_reason = "ops-unavailable";
  if (!ops) return 0;
  out->has_input = ops->has_any_input ? 1 : 0;
  out->maintenance_mode = login_maintenance_mode_active(ops);
  out->session_available = ops->session_ctx ? 1 : 0;
  out->settings_available = ops->settings ? 1 : 0;
  out->recovery_available = ops->maintenance_session_start ? 1 : 0;
  out->shell_callbacks_ready =
      (ops->prepare_shell_runtime && ops->init_shell_context_user &&
       ops->dispatch_shell_command && ops->run_shell_alias &&
       ops->is_equal && ops->shell_context_init &&
       ops->shell_context_should_logout) ? 1 : 0;
  out->auth_callbacks_ready =
      (ops->session_reset && ops->session_set_active &&
       ops->system_login && ops->session_user && ops->session_cwd) ? 1 : 0;
  out->ui_callbacks_ready =
      (ops->readline && ops->print && ops->putc && ops->clear_view &&
       ops->show_splash && ops->ui_banner && ops->cmd_info) ? 1 : 0;
  if (!out->session_available) {
    out->blocked_reason = "session-unavailable";
    return 0;
  }
  if (!out->settings_available) {
    out->blocked_reason = "settings-unavailable";
    return 0;
  }
  if (!out->shell_callbacks_ready) {
    out->blocked_reason = "shell-unavailable";
    return 0;
  }
  if (!out->auth_callbacks_ready) {
    out->blocked_reason = "auth-unavailable";
    return 0;
  }
  if (!out->ui_callbacks_ready) {
    out->blocked_reason = "ui-unavailable";
    return 0;
  }
  if (out->maintenance_mode && !out->recovery_available) {
    out->blocked_reason = "recovery-unavailable";
    return 0;
  }
  if (!ops_ready(ops)) {
    out->blocked_reason = "runtime-incomplete";
    return 0;
  }
  if (!out->has_input) {
    out->blocked_reason = "input-unavailable";
    return 0;
  }
  if (out->maintenance_mode) {
    out->blocked_reason = "maintenance-mode";
    return 0;
  }
  out->ready = 1;
  out->blocked_reason = "ready";
  return 0;
}



int login_recovery_resume_policy_evaluate(
    const struct login_runtime_ops *ops, int recovery_session_active,
    int resume_requested, struct login_recovery_resume_policy *out) {
  if (!out) return -1;
  out->version = LOGIN_RECOVERY_RESUME_POLICY_VERSION;
  out->recovery_session_active = recovery_session_active ? 1 : 0;
  out->resume_requested = resume_requested ? 1 : 0;
  out->maintenance_mode_active = ops ? login_maintenance_mode_active(ops) : 1;
  out->runtime_ready = ops_ready(ops) ? 1 : 0;
  out->can_resume_normal_login = 0;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->blocked_reason = "ops-unavailable";
  if (!ops) return 0;
  if (!out->recovery_session_active) {
    out->blocked_reason = "recovery-session-inactive";
    return 0;
  }
  if (!out->resume_requested) {
    out->blocked_reason = "resume-not-requested";
    return 0;
  }
  if (!out->runtime_ready) {
    out->blocked_reason = "runtime-incomplete";
    return 0;
  }
  if (out->maintenance_mode_active) {
    out->blocked_reason = "maintenance-mode-active";
    return 0;
  }
  out->can_resume_normal_login = 1;
  out->blocked_reason = "ready";
  return 0;
}

int login_window_credential_policy_from_contract(
    const struct login_window_contract *contract,
    struct login_window_credential_policy *out) {
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_POLICY_VERSION;
  out->max_password_chars = LOGIN_WINDOW_PASSWORD_MAX_CHARS;
  out->mask_char = LOGIN_WINDOW_PASSWORD_MASK_CHAR;
  out->password_field_allowed = 0;
  out->password_submit_allowed = 0;
  out->password_mask_required = 1;
  out->password_wipe_required = 1;
  out->recovery_allowed = 0;
  out->recovery_requires_text_session = 1;
  out->text_login_authoritative = 1;
  out->blocked_reason = "contract-unavailable";
  if (!contract) return 0;
  out->blocked_reason = contract->blocked_reason ? contract->blocked_reason
                                                 : "blocked";
  if (contract->ready) {
    out->password_field_allowed = 1;
    return 0;
  }
  if (contract->maintenance_mode && contract->has_input &&
      contract->session_available && contract->settings_available &&
      contract->shell_callbacks_ready && contract->auth_callbacks_ready &&
      contract->ui_callbacks_ready && contract->recovery_available) {
    out->recovery_allowed = 1;
  }
  return 0;
}
