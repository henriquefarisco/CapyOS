#ifndef AUTH_LOGIN_RUNTIME_COMMIT_DISPATCH_H
#define AUTH_LOGIN_RUNTIME_COMMIT_DISPATCH_H

/*
 * include/auth/login_runtime/commit_dispatch.h
 *
 * Internal partial header carrying the five window transaction plan
 * structs that sit above presenter_mount and feed the frame
 * compositor: commit, handoff, dispatch, queue, activation plans.
 *
 *   - struct login_window_credential_screen_commit_plan
 *   - struct login_window_credential_screen_handoff_plan
 *   - struct login_window_credential_screen_dispatch_plan
 *   - struct login_window_credential_screen_queue_plan
 *   - struct login_window_credential_screen_activation_plan
 *
 * Standalone-includable: only primitives + const char* fields.
 * PR B+C+D #6 of the dedicated plan extracts unchanged byte-for-byte.
 */

#include <stddef.h>

struct login_window_credential_screen_commit_plan {
  int version;
  int mount_plan_available;
  int mount_plan_safe;
  int commit_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_commit_required;
  int window_commit_allowed;
  int window_commit_executed;
  int widget_tree_selected;
  int commit_credential_panel;
  int commit_credential_input;
  int commit_credential_focus;
  int commit_text_recovery;
  int commit_text_login;
  int commit_text_login_resume;
  int commit_text_login_fallback;
  int commit_fallback_notice;
  int commit_text_login_notice;
  int commit_status;
  int commit_error;
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
  const char *commit_transaction;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_handoff_plan {
  int version;
  int commit_plan_available;
  int commit_plan_safe;
  int handoff_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_handoff_required;
  int window_handoff_allowed;
  int window_handoff_delivered;
  int envelope_selected;
  int handoff_credential_panel;
  int handoff_credential_input;
  int handoff_credential_focus;
  int handoff_text_recovery;
  int handoff_text_login;
  int handoff_text_login_resume;
  int handoff_text_login_fallback;
  int handoff_fallback_notice;
  int handoff_text_login_notice;
  int handoff_status;
  int handoff_error;
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
  const char *commit_transaction;
  const char *handoff_envelope;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_dispatch_plan {
  int version;
  int handoff_plan_available;
  int handoff_plan_safe;
  int dispatch_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_dispatch_required;
  int window_dispatch_allowed;
  int window_dispatch_delivered;
  int dispatch_ticket_selected;
  int dispatch_credential_panel;
  int dispatch_credential_input;
  int dispatch_credential_focus;
  int dispatch_text_recovery;
  int dispatch_text_login;
  int dispatch_text_login_resume;
  int dispatch_text_login_fallback;
  int dispatch_fallback_notice;
  int dispatch_text_login_notice;
  int dispatch_status;
  int dispatch_error;
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
  const char *commit_transaction;
  const char *handoff_envelope;
  const char *dispatch_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_queue_plan {
  int version;
  int dispatch_plan_available;
  int dispatch_plan_safe;
  int queue_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_queue_required;
  int window_queue_allowed;
  int window_queue_enqueued;
  int queue_ticket_selected;
  int queue_credential_panel;
  int queue_credential_input;
  int queue_credential_focus;
  int queue_text_recovery;
  int queue_text_login;
  int queue_text_login_resume;
  int queue_text_login_fallback;
  int queue_fallback_notice;
  int queue_text_login_notice;
  int queue_status;
  int queue_error;
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
  const char *commit_transaction;
  const char *handoff_envelope;
  const char *dispatch_ticket;
  const char *queue_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_activation_plan {
  int version;
  int queue_plan_available;
  int queue_plan_safe;
  int activation_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_activation_required;
  int window_activation_allowed;
  int window_activation_applied;
  int activation_ticket_selected;
  int activate_credential_panel;
  int activate_credential_input;
  int activate_credential_focus;
  int activate_text_recovery;
  int activate_text_login;
  int activate_text_login_resume;
  int activate_text_login_fallback;
  int activate_fallback_notice;
  int activate_text_login_notice;
  int activation_status;
  int activation_error;
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
  const char *commit_transaction;
  const char *handoff_envelope;
  const char *dispatch_ticket;
  const char *queue_ticket;
  const char *activation_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_COMMIT_DISPATCH_H */
