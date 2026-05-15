#include "auth/login_runtime.h"

#include <stddef.h>
#include <stdint.h>

static void loginwindow_session_handoff_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static int loginwindow_session_string_equal(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

static void loginwindow_session_handoff_defaults(
    struct login_window_credential_session_handoff *out) {
  loginwindow_session_handoff_wipe(out, sizeof(*out));
  out->version = LOGIN_WINDOW_CREDENTIAL_SESSION_HANDOFF_VERSION;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->text_login_authoritative = 1;
  out->session_reset_before_handoff_required = 1;
  out->fallback_text_login_allowed = 1;
  out->audit_required = 1;
  out->audit_redacted = 1;
  out->route = "force-text-login";
  out->handoff_target = "none";
  out->session_target = "none";
  out->event_type = "loginwindow-session-handoff-unavailable";
  out->state = "blocked";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "auth-submit-unavailable";
}

static int loginwindow_session_auth_submit_safe(
    const struct login_window_credential_auth_submit *auth_submit) {
  return auth_submit && auth_submit->gate_evaluated && auth_submit->auth_called &&
         auth_submit->credential_redacted && auth_submit->length_redacted &&
         !auth_submit->raw_secret_exposed && auth_submit->wipe_attempted &&
         auth_submit->wipe_succeeded;
}

static int loginwindow_session_recovery_decision_safe(
    const struct login_window_credential_recovery_decision *decision) {
  return decision && decision->decision_safe && decision->audit_redacted &&
         decision->credential_redacted && decision->length_redacted &&
         !decision->raw_secret_exposed && !decision->masked_text_exposed;
}

static int loginwindow_session_user_record_safe(
    const struct user_record *user) {
  return user && user->username[0];
}

static int loginwindow_session_desktop_user_eligible(
    const struct user_record *user) {
  if (!loginwindow_session_user_record_safe(user)) {
    return 0;
  }
  if (loginwindow_session_string_equal(user->username, "maintenance")) {
    return 0;
  }
  if (loginwindow_session_string_equal(user->role, "recovery")) {
    return 0;
  }
  return 1;
}

static void loginwindow_session_block(
    struct login_window_credential_session_handoff *out, const char *event_type,
    const char *state, const char *message, const char *blocked_reason) {
  out->handoff_safe = 0;
  out->user_record_consumed = 0;
  out->session_begin_required = 0;
  out->session_begin_allowed = 0;
  out->session_activate_allowed = 0;
  out->shell_context_init_required = 0;
  out->graphical_session_required = 0;
  out->graphical_session_allowed = 0;
  out->desktop_autostart_required = 0;
  out->logout_allowed = 0;
  out->fallback_text_login_allowed = out->text_login_authoritative ? 1 : 0;
  out->route = "force-text-login";
  out->handoff_target = "none";
  out->session_target = "none";
  out->event_type = event_type;
  out->state = state;
  out->message = message;
  out->blocked_reason = blocked_reason;
}

int login_window_credential_session_handoff_build(
    const struct login_window_credential_auth_submit *auth_submit,
    const struct login_window_credential_recovery_decision *recovery_decision,
    const struct user_record *authenticated_user,
    struct login_window_credential_session_handoff *out) {
  int recovery_route_active = 0;

  if (!out) {
    return -1;
  }
  loginwindow_session_handoff_defaults(out);
  out->auth_submit_available = auth_submit ? 1 : 0;
  out->recovery_decision_available = recovery_decision ? 1 : 0;
  out->user_record_available = loginwindow_session_user_record_safe(
                                   authenticated_user)
                                    ? 1
                                    : 0;

  if (auth_submit) {
    out->auth_called = auth_submit->auth_called ? 1 : 0;
    out->authenticated = auth_submit->authenticated ? 1 : 0;
    out->auth_failed = auth_submit->auth_failed ? 1 : 0;
    out->auth_locked = auth_submit->auth_locked ? 1 : 0;
    out->credential_redacted = auth_submit->credential_redacted ? 1 : 0;
    out->length_redacted = auth_submit->length_redacted ? 1 : 0;
    out->raw_secret_exposed = auth_submit->raw_secret_exposed ? 1 : 0;
    out->text_login_authoritative = auth_submit->text_login_authoritative ? 1 : 0;
    out->fallback_text_login_allowed = out->text_login_authoritative ? 1 : 0;
  }

  if (recovery_decision) {
    out->authenticated = (out->authenticated || recovery_decision->authenticated) ? 1 : 0;
    out->auth_failed = (out->auth_failed || recovery_decision->auth_failed) ? 1 : 0;
    out->auth_locked = (out->auth_locked || recovery_decision->auth_locked) ? 1 : 0;
    out->credential_redacted =
        (out->credential_redacted && recovery_decision->credential_redacted) ? 1 : 0;
    out->length_redacted =
        (out->length_redacted && recovery_decision->length_redacted) ? 1 : 0;
    out->raw_secret_exposed =
        (out->raw_secret_exposed || recovery_decision->raw_secret_exposed) ? 1 : 0;
    out->masked_text_exposed = recovery_decision->masked_text_exposed ? 1 : 0;
    out->text_login_authoritative =
        (out->text_login_authoritative && recovery_decision->text_login_authoritative)
            ? 1
            : 0;
    out->fallback_text_login_allowed = out->text_login_authoritative ? 1 : 0;
    out->lockout_blocked = recovery_decision->lockout_bypass_blocked ? 1 : 0;
    out->authenticated_recovery_blocked =
        recovery_decision->authenticated_recovery_blocked ? 1 : 0;
    recovery_route_active = recovery_decision->open_text_recovery_route ||
                            recovery_decision->resume_text_login_route ||
                            recovery_decision->recovery_allowed ||
                            recovery_decision->resume_allowed ||
                            recovery_decision->force_text_login_required;
    out->recovery_route_active = recovery_route_active ? 1 : 0;
  }

  out->auth_submit_safe = loginwindow_session_auth_submit_safe(auth_submit) ? 1 : 0;
  out->recovery_decision_safe =
      loginwindow_session_recovery_decision_safe(recovery_decision) ? 1 : 0;
  out->user_record_safe = loginwindow_session_user_record_safe(authenticated_user) ? 1 : 0;
  out->desktop_user_eligible =
      loginwindow_session_desktop_user_eligible(authenticated_user) ? 1 : 0;
  out->audit_redacted =
      (out->credential_redacted && out->length_redacted &&
       !out->raw_secret_exposed && !out->masked_text_exposed)
          ? 1
          : 0;

  if (!auth_submit) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unavailable",
                              "blocked", "Use authoritative text login.",
                              "auth-submit-unavailable");
    return 0;
  }
  if (!out->auth_submit_safe) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unsafe",
                              "blocked", "Authentication submit unsafe; use text login.",
                              "auth-submit-unsafe");
    return 0;
  }
  if (!recovery_decision) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unavailable",
                              "blocked", "Recovery decision unavailable; use text login.",
                              "recovery-decision-unavailable");
    return 0;
  }
  if (!out->recovery_decision_safe) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unsafe",
                              "blocked", "Recovery decision unsafe; use text login.",
                              "recovery-decision-unsafe");
    return 0;
  }
  if (!out->audit_redacted) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unsafe",
                              "blocked", "Session handoff audit unsafe; use text login.",
                              "audit-not-redacted");
    return 0;
  }
  if (!out->text_login_authoritative) {
    loginwindow_session_block(out, "loginwindow-session-handoff-unsafe",
                              "blocked", "Text login is not authoritative; session handoff blocked.",
                              "text-login-not-authoritative");
    return 0;
  }
  if (out->auth_locked || out->lockout_blocked) {
    out->lockout_blocked = 1;
    loginwindow_session_block(out, "loginwindow-session-handoff-lockout-blocked",
                              "locked", "Account locked; session handoff blocked.",
                              "auth-locked");
    return 0;
  }
  if (!out->authenticated) {
    loginwindow_session_block(out, "loginwindow-session-handoff-auth-blocked",
                              out->auth_failed ? "failed" : "blocked",
                              out->auth_failed ? "Credential authentication failed."
                                               : "Authentication did not complete.",
                              out->auth_failed ? "auth-failed"
                                               : "auth-not-authenticated");
    return 0;
  }
  if (!auth_submit->user_record_available ||
      !recovery_decision->user_record_available || !out->user_record_safe) {
    loginwindow_session_block(out, "loginwindow-session-handoff-user-blocked",
                              "blocked", "Authenticated user unavailable; use text login.",
                              "user-record-unavailable");
    return 0;
  }
  if (out->authenticated_recovery_blocked || out->recovery_route_active) {
    loginwindow_session_block(out, "loginwindow-session-handoff-recovery-blocked",
                              "blocked", "Recovery route active; session handoff blocked.",
                              out->authenticated_recovery_blocked
                                  ? "authenticated-recovery-blocked"
                                  : "recovery-route-active");
    return 0;
  }
  if (!out->desktop_user_eligible) {
    loginwindow_session_block(out, "loginwindow-session-handoff-user-blocked",
                              "blocked", "User is not eligible for graphical session handoff.",
                              "graphical-user-ineligible");
    return 0;
  }

  out->handoff_safe = 1;
  out->user_record_consumed = 1;
  out->session_begin_required = 1;
  out->session_begin_allowed = 1;
  out->session_activate_allowed = 1;
  out->shell_context_init_required = 1;
  out->graphical_session_required = 1;
  out->graphical_session_allowed = 1;
  out->desktop_autostart_required = 1;
  out->logout_allowed = 1;
  out->fallback_text_login_allowed = 0;
  out->route = "graphical-session";
  out->handoff_target = "x64-shell-session-desktop-autostart";
  out->session_target = "desktop-session";
  out->event_type = "loginwindow-session-handoff-ready";
  out->state = "graphical-session-ready";
  out->message = "Graphical session handoff ready.";
  out->blocked_reason = "ready";
  return 0;
}
