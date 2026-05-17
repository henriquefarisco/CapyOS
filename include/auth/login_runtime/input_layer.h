#ifndef AUTH_LOGIN_RUNTIME_INPUT_LAYER_H
#define AUTH_LOGIN_RUNTIME_INPUT_LAYER_H

/*
 * include/auth/login_runtime/input_layer.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the five input-layer structs that translate raw keyboard events
 * into safe credential panel state. They sit one stage above the
 * auth-core primitives (policy/buffer/submit) and feed the
 * audit_view stage above them.
 *
 *   - struct login_window_credential_input_result
 *   - struct login_window_credential_field_view
 *   - struct login_window_credential_panel
 *   - struct login_window_credential_interaction
 *   - struct login_window_credential_readiness
 *
 * INCLUSION CONTRACT
 * ------------------
 * Standalone-includable. Pulls in `<stddef.h>` (for `size_t`) and
 * `auth/login_runtime/auth_core.h` (for
 * `struct login_window_credential_submit_attempt` carried by value
 * inside `struct login_window_credential_input_result`).
 *
 * PR B+C+D #3 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`. No
 * behavioural change is intended; only the lexical home of the
 * declarations moves.
 */

#include <stddef.h>

#include "auth/login_runtime/auth_core.h"

struct login_window_credential_input_result {
  int version;
  int action;
  int accepted;
  int buffer_changed;
  int submit_attempted;
  int cancel_consumed;
  int wipe_attempted;
  int wipe_succeeded;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  int masked_text_required;
  size_t buffer_length;
  struct login_window_credential_submit_attempt submit_attempt;
  const char *blocked_reason;
};

struct login_window_credential_field_view {
  int version;
  int policy_available;
  int field_allowed;
  int buffer_available;
  int buffer_initialized;
  int has_secret;
  int masked_text_available;
  int masked_text_truncated;
  int masked_text_required;
  int wipe_required;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  size_t length;
  size_t max_chars;
  const char *state;
  const char *blocked_reason;
};

struct login_window_credential_panel {
  int version;
  int panel_renderable;
  int field_renderable;
  int input_result_available;
  int input_accepted;
  int input_blocked;
  int buffer_changed;
  int submit_attempted;
  int cancel_consumed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int masked_text_available;
  int masked_text_truncated;
  int masked_text_required;
  int has_secret;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  size_t length;
  size_t max_chars;
  struct login_window_credential_field_view field_view;
  const char *state;
  const char *blocked_reason;
};

struct login_window_credential_interaction {
  int version;
  int action;
  int input_applied;
  int panel_built;
  int input_accepted;
  int input_blocked;
  int buffer_changed;
  int submit_attempted;
  int cancel_consumed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int masked_text_available;
  int masked_text_truncated;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  size_t length;
  struct login_window_credential_input_result input_result;
  struct login_window_credential_panel panel;
  const char *state;
  const char *blocked_reason;
};

struct login_window_credential_readiness {
  int version;
  int policy_available;
  int field_allowed;
  int buffer_available;
  int buffer_initialized;
  int panel_available;
  int interaction_available;
  int ready_for_render;
  int ready_for_input;
  int ready_for_masked_text;
  int has_secret;
  int input_blocked;
  int submit_blocked;
  int cancel_consumed;
  int wipe_required;
  int wipe_attempted;
  int wipe_succeeded;
  int overflow_blocked;
  int masked_text_required;
  int masked_text_available;
  int masked_text_truncated;
  int submit_allowed;
  int auth_attempt_allowed;
  int text_login_authoritative;
  size_t length;
  size_t max_chars;
  const char *state;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_INPUT_LAYER_H */
