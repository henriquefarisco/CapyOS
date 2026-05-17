#ifndef AUTH_LOGIN_RUNTIME_CONTROLLER_HANDOFF_H
#define AUTH_LOGIN_RUNTIME_CONTROLLER_HANDOFF_H

/*
 * include/auth/login_runtime/controller_handoff.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the three controller / recovery decision / session handoff structs
 * that consolidate route plans into final UI controller decisions
 * and the desktop handoff contract.
 *
 *   - struct login_window_credential_screen_controller
 *   - struct login_window_credential_recovery_decision
 *   - struct login_window_credential_session_handoff
 *
 * INCLUSION CONTRACT
 * ------------------
 * Fully standalone. Only primitives + const char* fields.
 *
 * PR B+C+D #5 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`.
 */

#include <stddef.h>

struct login_window_credential_screen_controller {
  int version;
  int route_plan_available;
  int route_plan_safe;
  int controller_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int credential_screen_visible;
  int credential_input_focus;
  int text_recovery_open;
  int text_login_resume;
  int text_login_forced;
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

struct login_window_credential_recovery_decision {
  int version;
  int controller_available;
  int auth_submit_available;
  int controller_safe;
  int auth_submit_safe;
  int decision_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int stay_on_credential_screen;
  int open_text_recovery_route;
  int resume_text_login_route;
  int force_text_login_required;
  int recovery_allowed;
  int resume_allowed;
  int text_login_allowed;
  int credential_input_focus_allowed;
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
  int auth_called;
  int authenticated;
  int auth_failed;
  int auth_locked;
  int user_record_available;
  int lockout_bypass_blocked;
  int authenticated_recovery_blocked;
  int audit_required;
  int audit_redacted;
  const char *route;
  const char *result_action;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_session_handoff {
  int version;
  int auth_submit_available;
  int recovery_decision_available;
  int user_record_available;
  int auth_submit_safe;
  int recovery_decision_safe;
  int user_record_safe;
  int desktop_user_eligible;
  int handoff_safe;
  int auth_called;
  int authenticated;
  int auth_failed;
  int auth_locked;
  int user_record_consumed;
  int credential_redacted;
  int length_redacted;
  int raw_secret_exposed;
  int masked_text_exposed;
  int text_login_authoritative;
  int recovery_route_active;
  int lockout_blocked;
  int authenticated_recovery_blocked;
  int session_reset_before_handoff_required;
  int session_begin_required;
  int session_begin_allowed;
  int session_activate_allowed;
  int shell_context_init_required;
  int graphical_session_required;
  int graphical_session_allowed;
  int desktop_autostart_required;
  int logout_allowed;
  int fallback_text_login_allowed;
  int audit_required;
  int audit_redacted;
  const char *route;
  const char *handoff_target;
  const char *session_target;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_CONTROLLER_HANDOFF_H */
