#include "auth/login_runtime.h"

#include <stddef.h>
#include <stdint.h>

static void loginwindow_recovery_decision_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void loginwindow_recovery_decision_defaults(
    struct login_window_credential_recovery_decision *out) {
  loginwindow_recovery_decision_wipe(out, sizeof(*out));
  out->version = LOGIN_WINDOW_CREDENTIAL_RECOVERY_DECISION_VERSION;
  out->route_blocked = 1;
  out->force_text_login_required = 1;
  out->text_login_allowed = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->audit_required = 1;
  out->audit_redacted = 1;
  out->route = "force-text-login";
  out->result_action = "use-text-login";
  out->event_type = "credential-recovery-decision-unavailable";
  out->state = "blocked";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "controller-unavailable";
}

static int loginwindow_recovery_controller_safe(
    const struct login_window_credential_screen_controller *controller) {
  return controller && controller->controller_safe && controller->route_selected &&
         !controller->route_blocked && controller->credential_session_safe &&
         controller->credential_storage_wiped && controller->credential_redacted &&
         controller->length_redacted && !controller->raw_secret_exposed &&
         !controller->masked_text_exposed && controller->submit_blocked &&
         !controller->submit_enabled && !controller->auth_attempt_allowed;
}

static int loginwindow_recovery_auth_submit_safe(
    const struct login_window_credential_auth_submit *auth_submit) {
  if (!auth_submit) {
    return 1;
  }
  return auth_submit->credential_redacted && auth_submit->length_redacted &&
         !auth_submit->raw_secret_exposed && auth_submit->wipe_attempted &&
         auth_submit->wipe_succeeded;
}

int login_window_credential_recovery_decision_build(
    const struct login_window_credential_screen_controller *controller,
    const struct login_window_credential_auth_submit *auth_submit,
    struct login_window_credential_recovery_decision *out) {
  int controller_safe = 0;
  int auth_safe = 0;

  if (!out) {
    return -1;
  }
  loginwindow_recovery_decision_defaults(out);
  out->controller_available = controller ? 1 : 0;
  out->auth_submit_available = auth_submit ? 1 : 0;
  if (!controller) {
    return 0;
  }

  out->requested_action = controller->requested_action;
  out->route_selected = controller->route_selected ? 1 : 0;
  out->route_blocked = controller->route_blocked ? 1 : 0;
  out->stay_on_credential_screen =
      controller->credential_screen_visible && !controller->text_login_forced ? 1 : 0;
  out->open_text_recovery_route = controller->text_recovery_open ? 1 : 0;
  out->resume_text_login_route = controller->text_login_resume ? 1 : 0;
  out->force_text_login_required = controller->text_login_forced ? 1 : 0;
  out->credential_input_focus_allowed = controller->credential_input_focus ? 1 : 0;
  out->recovery_text_session_required =
      controller->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = controller->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      controller->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = controller->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = controller->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = controller->credential_redacted ? 1 : 0;
  out->length_redacted = controller->length_redacted ? 1 : 0;
  out->raw_secret_exposed = controller->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = controller->masked_text_exposed ? 1 : 0;
  out->submit_requested = controller->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = controller->text_login_authoritative ? 1 : 0;
  out->route = controller->route ? controller->route : "force-text-login";
  out->result_action = controller->result_action ? controller->result_action
                                                 : "use-text-login";
  out->event_type = controller->event_type ? controller->event_type
                                           : "credential-recovery-decision";
  out->state = controller->state ? controller->state : "blocked";
  out->message = controller->message ? controller->message
                                     : "Use authoritative text login.";
  out->blocked_reason = controller->blocked_reason ? controller->blocked_reason
                                                   : "blocked";

  if (auth_submit) {
    out->auth_called = auth_submit->auth_called ? 1 : 0;
    out->authenticated = auth_submit->authenticated ? 1 : 0;
    out->auth_failed = auth_submit->auth_failed ? 1 : 0;
    out->auth_locked = auth_submit->auth_locked ? 1 : 0;
    out->user_record_available = auth_submit->user_record_available ? 1 : 0;
    out->credential_redacted =
        (out->credential_redacted && auth_submit->credential_redacted) ? 1 : 0;
    out->length_redacted =
        (out->length_redacted && auth_submit->length_redacted) ? 1 : 0;
    out->raw_secret_exposed =
        (out->raw_secret_exposed || auth_submit->raw_secret_exposed) ? 1 : 0;
    out->submit_requested =
        (out->submit_requested || auth_submit->auth_called) ? 1 : 0;
    out->text_login_authoritative =
        (out->text_login_authoritative && auth_submit->text_login_authoritative)
            ? 1
            : 0;
  }

  controller_safe = loginwindow_recovery_controller_safe(controller);
  auth_safe = loginwindow_recovery_auth_submit_safe(auth_submit);
  out->controller_safe = controller_safe ? 1 : 0;
  out->auth_submit_safe = auth_safe ? 1 : 0;
  if (!controller_safe || !auth_safe) {
    out->route = "force-text-login";
    out->result_action = "use-text-login";
    out->event_type = "credential-recovery-decision-unsafe";
    out->state = "blocked";
    out->message = "Recovery route unsafe; use text login.";
    out->blocked_reason = !controller_safe ? "controller-unsafe"
                                           : "auth-submit-unsafe";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->stay_on_credential_screen = 0;
    out->open_text_recovery_route = 0;
    out->resume_text_login_route = 0;
    out->force_text_login_required = 1;
    out->credential_input_focus_allowed = 0;
    out->recovery_allowed = 0;
    out->resume_allowed = 0;
    out->text_login_allowed = out->text_login_authoritative ? 1 : 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }

  out->decision_safe = 1;
  out->text_login_allowed = out->text_login_authoritative ? 1 : 0;
  if (!out->text_login_authoritative) {
    out->decision_safe = 0;
    out->route = "force-text-login";
    out->result_action = "use-text-login";
    out->state = "blocked";
    out->message = "Text login is not authoritative; recovery blocked.";
    out->blocked_reason = "text-login-not-authoritative";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->force_text_login_required = 1;
    out->text_login_allowed = 0;
    return 0;
  }

  if (out->auth_locked) {
    out->lockout_bypass_blocked = 1;
    out->open_text_recovery_route = 0;
    out->resume_text_login_route = 0;
    out->stay_on_credential_screen = 0;
    out->recovery_allowed = 0;
    out->resume_allowed = 0;
    out->credential_input_focus_allowed = 0;
    out->route = "force-text-login";
    out->result_action = "use-text-login";
    out->event_type = "credential-recovery-lockout-blocked";
    out->state = "locked";
    out->message = "Account locked; recovery cannot bypass lockout.";
    out->blocked_reason = "auth-locked";
    out->route_selected = 1;
    out->route_blocked = 0;
    out->force_text_login_required = 1;
    return 0;
  }

  if (out->authenticated &&
      (out->open_text_recovery_route || out->resume_text_login_route)) {
    out->authenticated_recovery_blocked = 1;
    out->open_text_recovery_route = 0;
    out->resume_text_login_route = 0;
    out->recovery_allowed = 0;
    out->resume_allowed = 0;
    out->route = "force-text-login";
    out->result_action = "use-text-login";
    out->event_type = "credential-recovery-authenticated-blocked";
    out->state = "blocked";
    out->message = "Authenticated submit cannot enter recovery route.";
    out->blocked_reason = "authenticated-recovery-blocked";
    out->force_text_login_required = 1;
    return 0;
  }

  if (out->open_text_recovery_route) {
    if (!out->recovery_text_session_required) {
      out->decision_safe = 0;
      out->open_text_recovery_route = 0;
      out->route = "force-text-login";
      out->result_action = "use-text-login";
      out->state = "blocked";
      out->message = "Recovery requires an authoritative text session.";
      out->blocked_reason = "recovery-text-session-required";
      out->force_text_login_required = 1;
      return 0;
    }
    out->recovery_allowed = 1;
    out->text_login_allowed = 1;
    out->state = "recovery-ready";
    out->message = "Open text recovery route.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }

  if (out->resume_text_login_route) {
    if (!out->session_reset_required || !out->login_screen_rerender_required) {
      out->decision_safe = 0;
      out->resume_text_login_route = 0;
      out->route = "force-text-login";
      out->result_action = "use-text-login";
      out->state = "blocked";
      out->message = "Resume requires reset and rerender.";
      out->blocked_reason = "resume-reset-rerender-required";
      out->force_text_login_required = 1;
      return 0;
    }
    out->resume_allowed = 1;
    out->text_login_allowed = 1;
    out->state = "resume-ready";
    out->message = "Resume authoritative text login.";
    out->blocked_reason = "ready";
    return 0;
  }

  if (out->stay_on_credential_screen) {
    out->state = "credential-screen-ready";
    out->message = "Stay on credential screen.";
    out->blocked_reason = "ready";
    return 0;
  }

  out->route = "force-text-login";
  out->result_action = "use-text-login";
  out->force_text_login_required = 1;
  out->text_login_allowed = 1;
  out->state = "text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "text-login-authoritative";
  return 0;
}
