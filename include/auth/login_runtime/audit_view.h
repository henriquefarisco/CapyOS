#ifndef AUTH_LOGIN_RUNTIME_AUDIT_VIEW_H
#define AUTH_LOGIN_RUNTIME_AUDIT_VIEW_H

/*
 * include/auth/login_runtime/audit_view.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the four audit/view-model structs that consolidate input-layer
 * results into redacted audit events plus renderable view models
 * before they hand off to the screen pipeline.
 *
 *   - struct login_window_credential_audit_event
 *   - struct login_window_credential_view_model
 *   - struct login_window_credential_ui_step
 *   - struct login_window_credential_ui_session
 *
 * INCLUSION CONTRACT
 * ------------------
 * Fully standalone. The structs hold only primitives plus
 * `const char *` strings, so a single `<stddef.h>` (kept for `size_t`
 * future-proofing, even though the current shape does not use it
 * directly) is enough; no other partial header is needed.
 *
 * PR B+C+D #3 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`. No
 * behavioural change is intended; only the lexical home of the
 * declarations moves.
 */

#include <stddef.h>

struct login_window_credential_audit_event {
  int version;
  int action;
  int readiness_available;
  int interaction_available;
  int input_applied;
  int input_accepted;
  int input_blocked;
  int submit_attempted;
  int submit_blocked;
  int cancel_consumed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int ready_for_render;
  int ready_for_input;
  int ready_for_masked_text;
  int credential_present;
  int raw_secret_exposed;
  int masked_text_exposed;
  int secret_redacted;
  int length_redacted;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *event_type;
  const char *state;
  const char *blocked_reason;
};

struct login_window_credential_view_model {
  int version;
  int readiness_available;
  int audit_available;
  int renderable;
  int input_enabled;
  int password_field_visible;
  int masked_text_ready;
  int masked_text_redacted;
  int credential_present;
  int credential_redacted;
  int length_redacted;
  int submit_visible;
  int submit_enabled;
  int submit_blocked;
  int cancel_visible;
  int cancel_enabled;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int status_visible;
  int error_visible;
  int fallback_required;
  int raw_secret_exposed;
  int masked_text_exposed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_ui_step {
  int version;
  int action;
  int interaction_built;
  int readiness_built;
  int audit_built;
  int view_built;
  int input_applied;
  int input_accepted;
  int input_blocked;
  int buffer_changed;
  int renderable;
  int input_enabled;
  int credential_present;
  int credential_redacted;
  int length_redacted;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int raw_secret_exposed;
  int masked_text_exposed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_ui_session {
  int version;
  int action;
  int policy_available;
  int storage_available;
  int storage_cleared;
  int buffer_initialized;
  int step_built;
  int input_applied;
  int input_accepted;
  int input_blocked;
  int buffer_changed;
  int renderable;
  int input_enabled;
  int credential_present;
  int credential_redacted;
  int length_redacted;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int raw_secret_exposed;
  int masked_text_exposed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int storage_wiped;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_AUDIT_VIEW_H */
