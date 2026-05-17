#ifndef AUTH_LOGIN_RUNTIME_AUTH_CORE_H
#define AUTH_LOGIN_RUNTIME_AUTH_CORE_H

/*
 * include/auth/login_runtime/auth_core.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the six base structs of the authentication core (recovery resume
 * policy + credential policy/buffer/submit gate/submit attempt/auth
 * submit) plus the authenticator callback typedef. These structs feed
 * every higher pipeline stage (input layer, audit view, recovery
 * screen, controller/presenter, ...).
 *
 *   - struct login_recovery_resume_policy
 *   - struct login_window_credential_policy
 *   - struct login_window_credential_buffer
 *   - struct login_window_credential_submit_gate
 *   - struct login_window_credential_submit_attempt
 *   - typedef login_window_credential_authenticate_fn
 *   - struct login_window_credential_auth_submit
 *
 * INCLUSION CONTRACT
 * ------------------
 * This header is standalone-includable: it pulls in `<stddef.h>`
 * (for `size_t`) and `auth/user.h` (for `struct user_record`, which
 * appears as a pointer in the authenticator typedef).
 *
 * `include/auth/login_runtime/pipeline_contract.h` now includes this
 * header directly to resolve its previous transitional dependency on
 * the facade's inclusion order, so both partial headers can be
 * consumed individually.
 *
 * PR B+C+D #2 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`. No
 * behavioural change is intended; only the lexical home of the
 * declarations moves.
 */

#include <stddef.h>

#include "auth/user.h"

struct login_recovery_resume_policy {
  int version;
  int recovery_session_active;
  int resume_requested;
  int maintenance_mode_active;
  int runtime_ready;
  int can_resume_normal_login;
  int session_reset_required;
  int login_screen_rerender_required;
  const char *blocked_reason;
};

struct login_window_credential_policy {
  int version;
  size_t max_password_chars;
  char mask_char;
  int password_field_allowed;
  int password_submit_allowed;
  int password_mask_required;
  int password_wipe_required;
  int recovery_allowed;
  int recovery_requires_text_session;
  int text_login_authoritative;
  const char *blocked_reason;
};

struct login_window_credential_buffer {
  int version;
  char *storage;
  size_t capacity;
  size_t length;
  size_t max_chars;
  char mask_char;
  int initialized;
  int masked;
  int wipe_required;
  int submit_allowed;
  int overflow_blocked;
  int wiped;
  const char *blocked_reason;
};

struct login_window_credential_submit_gate {
  int version;
  int policy_available;
  int policy_submit_allowed;
  int buffer_available;
  int buffer_initialized;
  int buffer_has_secret;
  int buffer_masked;
  int buffer_submit_allowed;
  int wipe_required;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *blocked_reason;
};

struct login_window_credential_submit_attempt {
  int version;
  int attempted;
  int gate_evaluated;
  int buffer_had_secret;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  struct login_window_credential_submit_gate gate;
  const char *blocked_reason;
};

typedef int (*login_window_credential_authenticate_fn)(
    const char *username, const char *password, struct user_record *out);

struct login_window_credential_auth_submit {
  int version;
  int policy_available;
  int buffer_available;
  int username_available;
  int authenticate_available;
  int gate_evaluated;
  int attempted;
  int buffer_had_secret;
  int auth_called;
  int authenticated;
  int auth_failed;
  int auth_locked;
  int user_record_available;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  struct login_window_credential_submit_gate gate;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_AUTH_CORE_H */
