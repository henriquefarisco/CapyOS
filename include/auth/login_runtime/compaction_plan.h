#ifndef AUTH_LOGIN_RUNTIME_COMPACTION_PLAN_H
#define AUTH_LOGIN_RUNTIME_COMPACTION_PLAN_H

/*
 * include/auth/login_runtime/compaction_plan.h
 *
 * Standalone partial header for the
 *   struct login_window_credential_screen_compaction_plan
 * extracted byte-for-byte from `purge_reclaim.h` during PR 11a
 * (per-struct split to satisfy the 900-line audit rule). See
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include <stddef.h>

struct login_window_credential_screen_compaction_plan {
  int version;
  int tombstone_plan_available;
  int tombstone_plan_safe;
  int compaction_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int tombstone_required;
  int tombstone_allowed;
  int tombstone_submitted;
  int tombstone_ticket_selected;
  int tombstone_target_selected;
  int tombstone_persist_allowed;
  int tombstone_persisted;
  int tombstone_cpu_gpu_sync_allowed;
  int tombstone_cpu_gpu_sync_submitted;
  int compaction_required;
  int compaction_allowed;
  int compaction_submitted;
  int compaction_ticket_selected;
  int compaction_target_selected;
  int compaction_storage_write_allowed;
  int compaction_storage_written;
  int compaction_resource_release_allowed;
  int compaction_resource_released;
  int compaction_cpu_gpu_sync_allowed;
  int compaction_cpu_gpu_sync_submitted;
  int compaction_credential_panel;
  int compaction_credential_input;
  int compaction_credential_focus;
  int compaction_text_recovery;
  int compaction_text_login;
  int compaction_text_login_resume;
  int compaction_text_login_fallback;
  int compaction_error;
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
  const char *tombstone_ticket;
  const char *compaction_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *compositor_target;
  const char *tombstone_policy;
  const char *compaction_policy;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_COMPACTION_PLAN_H */
