#ifndef AUTH_LOGIN_RUNTIME_FRAME_COMPOSITOR_H
#define AUTH_LOGIN_RUNTIME_FRAME_COMPOSITOR_H

/*
 * include/auth/login_runtime/frame_compositor.h
 *
 * Internal partial header carrying the four frame / surface /
 * compositor / damage plan structs that translate window activation
 * tickets into compositor work items.
 *
 *   - struct login_window_credential_screen_frame_plan
 *   - struct login_window_credential_screen_surface_plan
 *   - struct login_window_credential_screen_compositor_plan
 *   - struct login_window_credential_screen_damage_plan
 *
 * Standalone-includable: only primitives + const char* fields.
 * PR B+C+D #6 of the dedicated plan extracts unchanged byte-for-byte.
 */

#include <stddef.h>

struct login_window_credential_screen_frame_plan {
  int version;
  int activation_plan_available;
  int activation_plan_safe;
  int frame_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_frame_required;
  int window_frame_allowed;
  int window_frame_rendered;
  int frame_ticket_selected;
  int frame_credential_panel;
  int frame_credential_input;
  int frame_credential_focus;
  int frame_text_recovery;
  int frame_text_login;
  int frame_text_login_resume;
  int frame_text_login_fallback;
  int frame_fallback_notice;
  int frame_text_login_notice;
  int frame_status;
  int frame_error;
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
  const char *frame_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_surface_plan {
  int version;
  int frame_plan_available;
  int frame_plan_safe;
  int surface_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_surface_required;
  int window_surface_allowed;
  int window_surface_submitted;
  int compositor_damage_planned;
  int compositor_damage_submitted;
  int surface_ticket_selected;
  int surface_credential_panel;
  int surface_credential_input;
  int surface_credential_focus;
  int surface_text_recovery;
  int surface_text_login;
  int surface_text_login_resume;
  int surface_text_login_fallback;
  int surface_error;
  int surface_reuse_allowed;
  int surface_cache_allowed;
  int full_damage_required;
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
  const char *activation_ticket;
  const char *frame_ticket;
  const char *surface_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *compositor_target;
  const char *damage_policy;
  const char *cache_policy;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_compositor_plan {
  int version;
  int surface_plan_available;
  int surface_plan_safe;
  int compositor_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int window_surface_allowed;
  int window_surface_submitted;
  int compositor_surface_required;
  int compositor_surface_allowed;
  int compositor_surface_submitted;
  int compositor_damage_planned;
  int compositor_damage_allowed;
  int compositor_damage_submitted;
  int compositor_ticket_selected;
  int compositor_credential_panel;
  int compositor_credential_input;
  int compositor_credential_focus;
  int compositor_text_recovery;
  int compositor_text_login;
  int compositor_text_login_resume;
  int compositor_text_login_fallback;
  int compositor_error;
  int compositor_reuse_allowed;
  int compositor_cache_allowed;
  int compositor_cache_hit;
  int full_damage_required;
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
  const char *surface_ticket;
  const char *compositor_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *compositor_target;
  const char *damage_policy;
  const char *cache_policy;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

struct login_window_credential_screen_damage_plan {
  int version;
  int compositor_plan_available;
  int compositor_plan_safe;
  int damage_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int compositor_surface_allowed;
  int compositor_surface_submitted;
  int compositor_damage_planned;
  int compositor_damage_allowed;
  int compositor_damage_submitted;
  int damage_required;
  int damage_allowed;
  int damage_submitted;
  int damage_ticket_selected;
  int damage_incremental_allowed;
  int full_damage_required;
  int damage_cache_allowed;
  int damage_cache_hit;
  int damage_reuse_allowed;
  int damage_credential_panel;
  int damage_credential_input;
  int damage_credential_focus;
  int damage_text_recovery;
  int damage_text_login;
  int damage_text_login_resume;
  int damage_text_login_fallback;
  int damage_error;
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
  const char *compositor_ticket;
  const char *damage_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *compositor_target;
  const char *damage_policy;
  const char *cache_policy;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_FRAME_COMPOSITOR_H */
