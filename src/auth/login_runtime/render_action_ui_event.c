/*
 * src/auth/login_runtime/render_action_ui_event.c
 *
 * Credential-screen rendering / action / UI-event plan builders —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during
 * PR C.7 of the Estagio C dedicated plan.  Hosts three contiguous
 * pipeline plan builders that translate the session view-model
 * into the first three pipeline-stage plans:
 *
 *   - login_window_credential_screen_render_plan_build
 *   - login_window_credential_screen_action_plan_build
 *   - login_window_credential_screen_ui_event_build
 *
 * The render-plan owns visual placement, the action-plan owns the
 * authoritative user action enum, and the UI-event plan stamps the
 * structured event that downstream route/controller/presenter
 * stages consume.  These three sit between the credential view
 * model (PR C.5) and the route/controller/presenter chain
 * (PR C.8+).
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_render_plan_build(
    const struct login_window_credential_screen_session *session,
    struct login_window_credential_screen_render_plan *out) {
  int session_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RENDER_PLAN_VERSION;
  out->session_available = session ? 1 : 0;
  out->screen_built = 0;
  out->screen_session_safe = 0;
  out->layout_visible = 0;
  out->header_visible = 0;
  out->status_visible = 0;
  out->error_visible = 1;
  out->renderable = 0;
  out->password_panel_visible = 0;
  out->password_input_visible = 0;
  out->password_input_enabled = 0;
  out->password_input_focus = 0;
  out->recovery_panel_visible = 0;
  out->recovery_button_visible = 0;
  out->recovery_button_enabled = 0;
  out->resume_button_visible = 0;
  out->resume_button_enabled = 0;
  out->maintenance_notice_visible = 0;
  out->fallback_notice_visible = 1;
  out->text_login_notice_visible = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->storage_cleared = 0;
  out->scratch_cleared = 0;
  out->submit_button_visible = 0;
  out->submit_button_enabled = 0;
  out->submit_blocked = 1;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->primary_action = "use-text-login";
  out->title = "CapyOS";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "screen-session-unavailable";
  if (!session) return 0;
  out->screen_built = session->screen_built ? 1 : 0;
  out->credential_session_safe = session->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = session->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = session->credential_redacted ? 1 : 0;
  out->length_redacted = session->length_redacted ? 1 : 0;
  out->raw_secret_exposed = session->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = session->masked_text_exposed ? 1 : 0;
  out->storage_cleared = session->storage_cleared ? 1 : 0;
  out->scratch_cleared = session->scratch_cleared ? 1 : 0;
  out->text_login_authoritative = session->text_login_authoritative ? 1 : 0;
  session_safe = session->screen_built && out->credential_session_safe &&
                 out->credential_storage_wiped && out->credential_redacted &&
                 out->length_redacted && !out->raw_secret_exposed &&
                 !out->masked_text_exposed && !session->submit_visible &&
                 session->submit_blocked && !session->submit_enabled &&
                 !session->auth_attempt_allowed && out->text_login_authoritative;
  if (!session_safe) {
    out->layout_visible = 1;
    out->header_visible = 1;
    out->status_visible = 1;
    out->error_visible = 1;
    out->state = "blocked";
    out->message = "Credential render plan unsafe; use text login.";
    out->blocked_reason = "credential-render-unsafe";
    return 0;
  }
  out->screen_session_safe = 1;
  out->layout_visible = 1;
  out->header_visible = 1;
  out->status_visible = 1;
  out->renderable = session->renderable ? 1 : 0;
  out->password_panel_visible = session->password_panel_visible ? 1 : 0;
  out->password_input_visible = session->password_panel_visible ? 1 : 0;
  out->password_input_enabled = session->password_input_enabled ? 1 : 0;
  out->recovery_panel_visible = session->recovery_visible || session->resume_visible ? 1 : 0;
  out->recovery_button_visible = session->recovery_visible ? 1 : 0;
  out->recovery_button_enabled = session->recovery_enabled ? 1 : 0;
  out->resume_button_visible = session->resume_visible ? 1 : 0;
  out->resume_button_enabled = session->resume_enabled ? 1 : 0;
  out->maintenance_notice_visible = session->maintenance_notice ? 1 : 0;
  out->fallback_notice_visible = session->fallback_required ? 1 : 0;
  out->text_login_notice_visible = 1;
  out->recovery_text_session_required = session->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = session->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = session->login_screen_rerender_required ? 1 : 0;
  out->submit_button_visible = 0;
  out->submit_button_enabled = 0;
  out->submit_blocked = 1;
  out->auth_attempt_allowed = 0;
  out->error_visible = session->renderable ? 0 : 1;
  out->title = session->title ? session->title : "CapyOS";
  out->state = session->state ? session->state : "blocked";
  out->message = session->message ? session->message
                                  : "Text login remains authoritative.";
  out->blocked_reason = session->blocked_reason ? session->blocked_reason
                                                : "blocked";
  if (out->resume_button_enabled) {
    out->primary_action = "resume-text-login";
  } else if (out->recovery_button_enabled) {
    out->primary_action = "open-text-recovery";
  } else if (out->password_input_enabled) {
    out->primary_action = "edit-credential";
    out->password_input_focus = 1;
  }
  return 0;
}


int login_window_credential_screen_action_plan_build(
    const struct login_window_credential_screen_render_plan *render_plan,
    int requested_action,
    struct login_window_credential_screen_action_plan *out) {
  int plan_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_PLAN_VERSION;
  out->render_plan_available = render_plan ? 1 : 0;
  out->render_plan_safe = 0;
  out->requested_action = requested_action;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->edit_credential_requested = 0;
  out->edit_credential_allowed = 0;
  out->open_text_recovery_requested = 0;
  out->open_text_recovery_allowed = 0;
  out->resume_text_login_requested = 0;
  out->resume_text_login_allowed = 0;
  out->use_text_login_required = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_requested = requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL ? 1 : 0;
  out->edit_credential_requested = requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL ? 1 : 0;
  out->open_text_recovery_requested = requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY ? 1 : 0;
  out->resume_text_login_requested = requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->result_action = "use-text-login";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "render-plan-unavailable";
  if (!render_plan) return 0;
  out->credential_session_safe = render_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = render_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = render_plan->credential_redacted ? 1 : 0;
  out->length_redacted = render_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = render_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = render_plan->masked_text_exposed ? 1 : 0;
  out->recovery_text_session_required = render_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = render_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = render_plan->login_screen_rerender_required ? 1 : 0;
  out->text_login_authoritative = render_plan->text_login_authoritative ? 1 : 0;
  plan_safe = render_plan->session_available && render_plan->screen_built &&
              render_plan->screen_session_safe && out->credential_session_safe &&
              out->credential_storage_wiped && out->credential_redacted &&
              out->length_redacted && !out->raw_secret_exposed &&
              !out->masked_text_exposed && !render_plan->submit_button_visible &&
              !render_plan->submit_button_enabled && render_plan->submit_blocked &&
              !render_plan->auth_attempt_allowed && out->text_login_authoritative;
  if (!plan_safe) {
    out->state = "blocked";
    out->message = "Credential action plan unsafe; use text login.";
    out->blocked_reason = "credential-action-unsafe";
    return 0;
  }
  out->render_plan_safe = 1;
  if (requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL) {
    out->edit_credential_requested = 1;
    if (render_plan->password_input_visible && render_plan->password_input_enabled) {
      out->action_allowed = 1;
      out->action_blocked = 0;
      out->input_focus_allowed = 1;
      out->edit_credential_allowed = 1;
      out->use_text_login_required = 0;
      out->result_action = "edit-credential";
      out->state = "action-ready";
      out->message = "Credential input can receive focus; text login remains authoritative.";
      out->blocked_reason = "ready";
      return 0;
    }
    out->blocked_reason = "credential-input-unavailable";
    return 0;
  }
  if (requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY) {
    out->open_text_recovery_requested = 1;
    if (render_plan->recovery_button_visible && render_plan->recovery_button_enabled &&
        render_plan->recovery_text_session_required) {
      out->action_allowed = 1;
      out->action_blocked = 0;
      out->open_text_recovery_allowed = 1;
      out->use_text_login_required = 1;
      out->result_action = "open-text-recovery";
      out->state = "text-recovery-action-ready";
      out->message = "Open text recovery session; graphical authentication remains disabled.";
      out->blocked_reason = "text-recovery-only";
      return 0;
    }
    out->blocked_reason = "text-recovery-unavailable";
    return 0;
  }
  if (requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN) {
    out->resume_text_login_requested = 1;
    if (render_plan->resume_button_visible && render_plan->resume_button_enabled &&
        render_plan->session_reset_required &&
        render_plan->login_screen_rerender_required) {
      out->action_allowed = 1;
      out->action_blocked = 0;
      out->resume_text_login_allowed = 1;
      out->use_text_login_required = 1;
      out->result_action = "resume-text-login";
      out->state = "resume-action-ready";
      out->message = "Resume normal text login; graphical authentication remains disabled.";
      out->blocked_reason = "ready";
      return 0;
    }
    out->blocked_reason = "resume-text-login-unavailable";
    return 0;
  }
  if (requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_USE_TEXT_LOGIN ||
      requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_NONE) {
    out->action_allowed = 1;
    out->action_blocked = 0;
    out->use_text_login_required = 1;
    out->result_action = "use-text-login";
    out->state = "text-login-required";
    out->message = "Use authoritative text login.";
    out->blocked_reason = "text-login-authoritative";
    return 0;
  }
  if (requested_action == LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL) {
    out->submit_requested = 1;
    out->state = "blocked";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  out->blocked_reason = "credential-action-unknown";
  return 0;
}


int login_window_credential_screen_ui_event_build(
    const struct login_window_credential_screen_action_plan *action_plan,
    struct login_window_credential_screen_ui_event *out) {
  int action_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_UI_EVENT_VERSION;
  out->action_plan_available = action_plan ? 1 : 0;
  out->action_plan_safe = 0;
  out->ui_event_safe = 0;
  out->requested_action = 0;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->edit_credential_requested = 0;
  out->open_text_recovery_requested = 0;
  out->resume_text_login_requested = 0;
  out->use_text_login_required = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_requested = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->event_type = "credential-screen-action-unavailable";
  out->result_action = "use-text-login";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "action-plan-unavailable";
  if (!action_plan) return 0;
  out->requested_action = action_plan->requested_action;
  out->action_allowed = action_plan->action_allowed ? 1 : 0;
  out->action_blocked = action_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = action_plan->input_focus_allowed ? 1 : 0;
  out->edit_credential_requested = action_plan->edit_credential_requested ? 1 : 0;
  out->open_text_recovery_requested = action_plan->open_text_recovery_requested ? 1 : 0;
  out->resume_text_login_requested = action_plan->resume_text_login_requested ? 1 : 0;
  out->use_text_login_required = action_plan->use_text_login_required ? 1 : 0;
  out->recovery_text_session_required = action_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = action_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = action_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = action_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = action_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = action_plan->credential_redacted ? 1 : 0;
  out->length_redacted = action_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = action_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = action_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = action_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = action_plan->text_login_authoritative ? 1 : 0;
  out->result_action = action_plan->result_action ? action_plan->result_action
                                                  : "use-text-login";
  out->state = action_plan->state ? action_plan->state : "blocked";
  out->message = action_plan->message ? action_plan->message
                                      : "Text login remains authoritative.";
  out->blocked_reason = action_plan->blocked_reason ? action_plan->blocked_reason
                                                    : "blocked";
  action_safe = action_plan->render_plan_available &&
                action_plan->render_plan_safe && out->credential_session_safe &&
                out->credential_storage_wiped && out->credential_redacted &&
                out->length_redacted && !out->raw_secret_exposed &&
                !out->masked_text_exposed && action_plan->submit_blocked &&
                !action_plan->submit_enabled && !action_plan->auth_attempt_allowed &&
                out->text_login_authoritative;
  if (!action_safe) {
    out->event_type = "credential-screen-action-unsafe";
    out->result_action = "use-text-login";
    out->state = "blocked";
    out->message = "Credential UI event unsafe; use text login.";
    out->blocked_reason = "credential-ui-event-unsafe";
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->action_plan_safe = 1;
  out->ui_event_safe = 1;
  if (out->submit_requested) {
    out->event_type = "credential-screen-submit-blocked";
    out->result_action = "use-text-login";
    out->state = "blocked";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    return 0;
  }
  if (out->action_allowed && !out->action_blocked &&
      action_plan->edit_credential_allowed && out->input_focus_allowed) {
    out->event_type = "credential-screen-edit-focus";
    out->result_action = "edit-credential";
    return 0;
  }
  if (out->action_allowed && !out->action_blocked &&
      action_plan->open_text_recovery_allowed &&
      out->recovery_text_session_required) {
    out->event_type = "credential-screen-open-text-recovery";
    out->result_action = "open-text-recovery";
    return 0;
  }
  if (out->action_allowed && !out->action_blocked &&
      action_plan->resume_text_login_allowed && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->event_type = "credential-screen-resume-text-login";
    out->result_action = "resume-text-login";
    return 0;
  }
  if (out->action_allowed && out->use_text_login_required) {
    out->event_type = "credential-screen-use-text-login";
    out->result_action = "use-text-login";
    return 0;
  }
  out->event_type = "credential-screen-action-blocked";
  out->result_action = "use-text-login";
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  return 0;
}
