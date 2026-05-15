#include "auth/login_runtime.h"

#include <stddef.h>
#include <stdint.h>

static void loginwindow_auth_wipe(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void loginwindow_auth_user_clear(struct user_record *user) {
  if (user) {
    loginwindow_auth_wipe(user, sizeof(*user));
  }
}

static void loginwindow_auth_submit_defaults(
    struct login_window_credential_auth_submit *out) {
  loginwindow_auth_wipe(out, sizeof(*out));
  out->version = LOGIN_WINDOW_CREDENTIAL_AUTH_SUBMIT_VERSION;
  out->attempted = 1;
  out->wipe_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->text_login_authoritative = 1;
  out->state = "blocked";
  out->message = "Graphical credential submit unavailable.";
  out->blocked_reason = "policy-unavailable";
  out->gate.version = LOGIN_WINDOW_CREDENTIAL_SUBMIT_GATE_VERSION;
  out->gate.wipe_required = 1;
  out->gate.text_login_authoritative = 1;
  out->gate.blocked_reason = "not-evaluated";
}

static void loginwindow_auth_finish_wipe(
    struct login_window_credential_buffer *buffer,
    struct login_window_credential_auth_submit *out) {
  int wipe_rc = 0;
  if (!buffer) {
    return;
  }
  out->wipe_attempted = 1;
  wipe_rc = login_window_credential_buffer_wipe(buffer);
  out->wipe_succeeded = wipe_rc == 0 ? 1 : 0;
  if (wipe_rc != 0) {
    out->authenticated = 0;
    out->auth_failed = 1;
    out->user_record_available = 0;
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->state = "blocked";
    out->message = "Credential storage wipe failed; use text login.";
    out->blocked_reason = "credential-wipe-failed";
  }
}

int login_window_credential_auth_policy_from_contract(
    const struct login_window_contract *contract,
    struct login_window_credential_policy *out) {
  if (login_window_credential_policy_from_contract(contract, out) != 0) {
    return -1;
  }
  if (!contract || !out) {
    return out ? 0 : -1;
  }
  if (contract->ready && out->password_field_allowed &&
      out->password_mask_required && out->password_wipe_required &&
      out->text_login_authoritative) {
    out->password_submit_allowed = 1;
    out->blocked_reason = "ready";
  }
  return 0;
}

int login_window_credential_auth_submit_consume(
    const struct login_window_credential_policy *policy,
    const char *username,
    struct login_window_credential_buffer *buffer,
    login_window_credential_authenticate_fn authenticate,
    struct user_record *out_user,
    struct login_window_credential_auth_submit *out) {
  struct user_record local_user;
  int auth_rc = USERDB_AUTH_FAILED;

  if (!out) {
    return -1;
  }
  loginwindow_auth_submit_defaults(out);
  loginwindow_auth_user_clear(out_user);
  loginwindow_auth_user_clear(&local_user);

  out->policy_available = policy ? 1 : 0;
  out->buffer_available = buffer ? 1 : 0;
  out->username_available = username && username[0] ? 1 : 0;
  out->authenticate_available = authenticate ? 1 : 0;
  out->buffer_had_secret = buffer && buffer->length > 0 ? 1 : 0;

  if (login_window_credential_submit_gate_evaluate(policy, buffer,
                                                   &out->gate) != 0) {
    loginwindow_auth_finish_wipe(buffer, out);
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->blocked_reason = "submit-gate-failed";
    return -1;
  }
  out->gate_evaluated = 1;
  out->submit_allowed = out->gate.submit_allowed ? 1 : 0;
  out->auth_attempt_allowed = out->gate.auth_attempt_allowed ? 1 : 0;
  out->text_login_authoritative = out->gate.text_login_authoritative ? 1 : 0;
  out->wipe_required = out->gate.wipe_required ? 1 : 0;
  out->blocked_reason = out->gate.blocked_reason ? out->gate.blocked_reason
                                                 : "gui-submit-disabled";

  if (!out->submit_allowed || !out->auth_attempt_allowed) {
    loginwindow_auth_finish_wipe(buffer, out);
    loginwindow_auth_user_clear(&local_user);
    return 0;
  }
  if (!out->username_available) {
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->state = "blocked";
    out->message = "Username unavailable; use text login.";
    out->blocked_reason = "username-unavailable";
    loginwindow_auth_finish_wipe(buffer, out);
    loginwindow_auth_user_clear(&local_user);
    return 0;
  }
  if (!out_user) {
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->state = "blocked";
    out->message = "User output unavailable; use text login.";
    out->blocked_reason = "user-output-unavailable";
    loginwindow_auth_finish_wipe(buffer, out);
    loginwindow_auth_user_clear(&local_user);
    return 0;
  }
  if (!out->authenticate_available) {
    out->submit_allowed = 0;
    out->auth_attempt_allowed = 0;
    out->state = "blocked";
    out->message = "Authentication callback unavailable; use text login.";
    out->blocked_reason = "auth-callback-unavailable";
    loginwindow_auth_finish_wipe(buffer, out);
    loginwindow_auth_user_clear(&local_user);
    return 0;
  }

  out->auth_called = 1;
  auth_rc = authenticate(username, buffer->storage, &local_user);
  loginwindow_auth_finish_wipe(buffer, out);
  if (!out->wipe_succeeded) {
    loginwindow_auth_user_clear(out_user);
    loginwindow_auth_user_clear(&local_user);
    return 0;
  }

  if (auth_rc == USERDB_AUTH_OK) {
    out->authenticated = 1;
    out->state = "authenticated";
    out->message = "Graphical credential authenticated.";
    out->blocked_reason = "ready";
    *out_user = local_user;
    out->user_record_available = 1;
  } else if (auth_rc == USERDB_AUTH_LOCKED) {
    out->auth_locked = 1;
    out->state = "locked";
    out->message = "Account temporarily locked.";
    out->blocked_reason = "auth-locked";
  } else {
    out->auth_failed = 1;
    out->state = "failed";
    out->message = "Credential authentication failed.";
    out->blocked_reason = "auth-failed";
  }

  if (!out->authenticated) {
    loginwindow_auth_user_clear(out_user);
  }
  loginwindow_auth_user_clear(&local_user);
  return 0;
}
