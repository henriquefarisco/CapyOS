/*
 * src/auth/login_runtime/session_pipeline.c
 *
 * Credential-screen session lifecycle — extracted byte-for-byte
 * from `src/auth/login_runtime.c` during PR C.6 of the Estagio C
 * dedicated plan.  Hosts three functions that together produce the
 * initial credential-screen session struct consumed by every
 * downstream pipeline stage:
 *
 *   - login_window_credential_screen_session_clear_io  (static helper)
 *   - login_window_credential_screen_session_defaults  (static helper)
 *   - login_window_credential_screen_session_build     (public)
 *
 * `_clear_io` and `_defaults` keep their `static` storage class
 * because they are only consumed by `_session_build` within this
 * translation unit (verified pre-extraction; no external callers).
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_session_clear_io(
    char *storage, size_t storage_size,
    char *masked_scratch, size_t masked_scratch_size) {
  size_t idx = 0;
  if (storage && storage_size > 0) {
    for (idx = 0; idx < storage_size; ++idx) {
      storage[idx] = '\0';
    }
  }
  if (masked_scratch && masked_scratch_size > 0) {
    masked_scratch[0] = '\0';
  }
}

static void login_window_credential_screen_session_defaults(
    struct login_window_credential_screen_session *out) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SESSION_VERSION;
  out->ops_available = 0;
  out->contract_built = 0;
  out->login_view_built = 0;
  out->policy_built = 0;
  out->credential_session_built = 0;
  out->resume_policy_built = 0;
  out->recovery_view_built = 0;
  out->screen_built = 0;
  out->renderable = 0;
  out->password_panel_visible = 0;
  out->password_input_enabled = 0;
  out->recovery_visible = 0;
  out->recovery_enabled = 0;
  out->resume_visible = 0;
  out->resume_enabled = 0;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->fallback_required = 1;
  out->maintenance_notice = 0;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->storage_cleared = 0;
  out->scratch_cleared = 0;
  out->submit_visible = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->title = "CapyOS";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "ops-unavailable";
}

int login_window_credential_screen_session_build(
    const struct login_runtime_ops *ops, const char *language,
    char *storage, size_t storage_size, int action, char ch,
    char *masked_scratch, size_t masked_scratch_size,
    int recovery_session_active, int resume_requested,
    struct login_window_credential_screen_session *out) {
  struct login_window_contract contract;
  struct login_window_view_model login_view;
  struct login_window_credential_policy policy;
  struct login_window_credential_ui_session credential_session;
  struct login_recovery_resume_policy resume_policy;
  struct login_window_credential_recovery_view_model recovery_view;
  struct login_window_credential_screen_view_model screen;
  struct login_recovery_resume_policy *resume_ptr = 0;
  login_window_credential_screen_session_clear_io(storage, storage_size,
                                                  masked_scratch,
                                                  masked_scratch_size);
  if (!out) return -1;
  login_window_credential_screen_session_defaults(out);
  out->ops_available = ops ? 1 : 0;
  out->storage_cleared = storage && storage_size > 0 ? 1 : 0;
  out->scratch_cleared = masked_scratch && masked_scratch_size > 0 ? 1 : 0;
  if (!ops) return 0;
  if (login_window_contract_evaluate(ops, &contract) != 0) {
    out->blocked_reason = "contract-build-failed";
    return -1;
  }
  out->contract_built = 1;
  if (login_window_view_model_build(&contract, language, &login_view) != 0) {
    out->blocked_reason = "login-view-build-failed";
    return -1;
  }
  out->login_view_built = 1;
  if (login_window_credential_policy_from_contract(&contract, &policy) != 0) {
    out->blocked_reason = "credential-policy-build-failed";
    return -1;
  }
  out->policy_built = 1;
  if (login_window_credential_ui_session_build(
          &policy, storage, storage_size, action, ch, masked_scratch,
          masked_scratch_size, &credential_session) != 0) {
    out->blocked_reason = "credential-session-build-failed";
    out->storage_cleared = storage && storage_size > 0 ? 1 : 0;
    out->scratch_cleared = masked_scratch && masked_scratch_size > 0 ? 1 : 0;
    return -1;
  }
  out->credential_session_built = 1;
  out->storage_cleared = credential_session.storage_cleared ? 1 : 0;
  out->credential_storage_wiped = credential_session.storage_wiped ? 1 : 0;
  if (login_recovery_resume_policy_evaluate(ops, recovery_session_active,
                                            resume_requested,
                                            &resume_policy) != 0) {
    out->blocked_reason = "resume-policy-build-failed";
    return -1;
  }
  out->resume_policy_built = 1;
  resume_ptr = (recovery_session_active || resume_requested) ? &resume_policy : 0;
  if (login_window_credential_recovery_view_model_build(
          &contract, &policy, &credential_session, resume_ptr,
          &recovery_view) != 0) {
    out->blocked_reason = "recovery-view-build-failed";
    return -1;
  }
  out->recovery_view_built = 1;
  if (login_window_credential_screen_view_model_build(
          &contract, &login_view, &credential_session, &recovery_view,
          &screen) != 0) {
    out->blocked_reason = "screen-build-failed";
    return -1;
  }
  out->screen_built = 1;
  out->renderable = screen.renderable ? 1 : 0;
  out->password_panel_visible = screen.password_panel_visible ? 1 : 0;
  out->password_input_enabled = screen.password_input_enabled ? 1 : 0;
  out->recovery_visible = screen.recovery_visible ? 1 : 0;
  out->recovery_enabled = screen.recovery_enabled ? 1 : 0;
  out->resume_visible = screen.resume_visible ? 1 : 0;
  out->resume_enabled = screen.resume_enabled ? 1 : 0;
  out->recovery_text_session_required = screen.recovery_text_session_required ? 1 : 0;
  out->session_reset_required = screen.session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = screen.login_screen_rerender_required ? 1 : 0;
  out->fallback_required = screen.fallback_required ? 1 : 0;
  out->maintenance_notice = screen.maintenance_notice ? 1 : 0;
  out->credential_session_safe = screen.credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = screen.credential_storage_wiped ? 1 : 0;
  out->credential_redacted = screen.credential_redacted ? 1 : 0;
  out->length_redacted = screen.length_redacted ? 1 : 0;
  out->raw_secret_exposed = screen.raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = screen.masked_text_exposed ? 1 : 0;
  out->submit_visible = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = screen.text_login_authoritative ? 1 : 0;
  out->title = screen.title ? screen.title : "CapyOS";
  out->state = screen.state ? screen.state : "blocked";
  out->message = screen.message ? screen.message
                                : "Text login remains authoritative.";
  out->blocked_reason = screen.blocked_reason ? screen.blocked_reason
                                              : "blocked";
  if (masked_scratch && masked_scratch_size > 0) {
    masked_scratch[0] = '\0';
    out->scratch_cleared = 1;
  }
  return 0;
}
