#ifndef AUTH_LOGIN_RUNTIME_ACTION_ROUTE_H
#define AUTH_LOGIN_RUNTIME_ACTION_ROUTE_H

/*
 * include/auth/login_runtime/action_route.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the three action/UI-event/route structs that translate the
 * render plan from `recovery_screen` into validated navigation
 * intents before the controller layer.
 *
 *   - struct login_window_credential_screen_action_plan
 *   - struct login_window_credential_screen_ui_event
 *   - struct login_window_credential_screen_route_plan
 *
 * INCLUSION CONTRACT
 * ------------------
 * Fully standalone. Only primitives + const char* fields.
 *
 * PR B+C+D #5 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`.
 */

#include <stddef.h>

struct login_window_credential_screen_action_plan {
  int version;
  int render_plan_available;
  int render_plan_safe;
  int requested_action;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int edit_credential_requested;
  int edit_credential_allowed;
  int open_text_recovery_requested;
  int open_text_recovery_allowed;
  int resume_text_login_requested;
  int resume_text_login_allowed;
  int use_text_login_required;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int submit_requested;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *result_action;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_ui_event {
  int version;
  int action_plan_available;
  int action_plan_safe;
  int ui_event_safe;
  int requested_action;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int edit_credential_requested;
  int open_text_recovery_requested;
  int resume_text_login_requested;
  int use_text_login_required;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int submit_requested;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *event_type;
  const char *result_action;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_route_plan {
  int version;
  int ui_event_available;
  int ui_event_safe;
  int route_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int stay_on_credential_screen;
  int open_text_recovery_route;
  int resume_text_login_route;
  int force_text_login_required;
  int recovery_text_session_required;
  int session_reset_required;
  int login_screen_rerender_required;
  int credential_session_safe;
  int credential_storage_wiped;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int submit_requested;
  int submit_blocked;
  int submit_enabled;
  int auth_attempt_allowed;
  int text_login_authoritative;
  const char *route;
  const char *event_type;
  const char *result_action;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_ACTION_ROUTE_H */
