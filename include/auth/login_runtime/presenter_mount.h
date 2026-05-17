#ifndef AUTH_LOGIN_RUNTIME_PRESENTER_MOUNT_H
#define AUTH_LOGIN_RUNTIME_PRESENTER_MOUNT_H

/*
 * include/auth/login_runtime/presenter_mount.h
 *
 * Internal partial header of the `login_runtime` module: it carries
 * the three presenter/binding/mount-plan structs that translate
 * controller decisions into safe widget tree mount plans for the
 * compositor.
 *
 *   - struct login_window_credential_screen_presenter
 *   - struct login_window_credential_screen_binding
 *   - struct login_window_credential_screen_mount_plan
 *
 * INCLUSION CONTRACT
 * ------------------
 * Fully standalone. Only primitives + const char* fields.
 *
 * PR B+C+D #5 of the dedicated plan extracts these definitions
 * unchanged byte-for-byte from `auth/login_runtime.h`.
 */

#include <stddef.h>

struct login_window_credential_screen_presenter {
  int version;
  int controller_available;
  int controller_safe;
  int presenter_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int credential_screen_visible;
  int credential_panel_visible;
  int credential_input_visible;
  int credential_input_focus;
  int text_recovery_visible;
  int text_recovery_open;
  int text_login_visible;
  int text_login_resume;
  int text_login_forced;
  int fallback_notice_visible;
  int text_login_notice_visible;
  int status_visible;
  int error_visible;
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
  const char *view;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_binding {
  int version;
  int presenter_available;
  int presenter_safe;
  int binding_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_mount_required;
  int credential_panel_bound;
  int credential_input_bound;
  int credential_input_focus_requested;
  int text_recovery_bound;
  int text_login_bound;
  int text_login_resume_bound;
  int text_login_fallback_bound;
  int fallback_notice_bound;
  int text_login_notice_bound;
  int status_bound;
  int error_bound;
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
  const char *view;
  const char *widget_tree;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_mount_plan {
  int version;
  int binding_available;
  int binding_safe;
  int mount_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_transaction_required;
  int window_mount_allowed;
  int widget_tree_selected;
  int mount_credential_panel;
  int mount_credential_input;
  int request_credential_focus;
  int mount_text_recovery;
  int mount_text_login;
  int mount_text_login_resume;
  int mount_text_login_fallback;
  int mount_fallback_notice;
  int mount_text_login_notice;
  int mount_status;
  int mount_error;
  int submit_callback_bound;
  int auth_callback_bound;
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
  const char *view;
  const char *widget_tree;
  const char *mount_transaction;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_PRESENTER_MOUNT_H */
