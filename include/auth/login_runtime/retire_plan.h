#ifndef AUTH_LOGIN_RUNTIME_RETIRE_PLAN_H
#define AUTH_LOGIN_RUNTIME_RETIRE_PLAN_H

/*
 * include/auth/login_runtime/retire_plan.h
 *
 * Standalone partial header for the
 *   struct login_window_credential_screen_retire_plan
 * extracted byte-for-byte from `deadline_cleanup.h` during the
 * 2026-05-16 preventive refactor (per-struct split to satisfy the
 * 900-line audit rule with comfortable headroom). See
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include <stddef.h>

struct login_window_credential_screen_retire_plan {
  int version;
  int ack_plan_available;
  int ack_plan_safe;
  int retire_plan_safe;
  int requested_action;
  int route_selected;
  int route_blocked;
  int action_allowed;
  int action_blocked;
  int input_focus_allowed;
  int ack_required;
  int ack_allowed;
  int ack_submitted;
  int ack_ticket_selected;
  int ack_target_selected;
  int ack_cpu_gpu_sync_allowed;
  int ack_cpu_gpu_sync_submitted;
  int completion_required;
  int completion_allowed;
  int completion_report_required;
  int completion_reported;
  int completion_ack_required;
  int completion_acknowledged;
  int completion_ticket_selected;
  int completion_target_selected;
  int completion_cpu_gpu_sync_allowed;
  int completion_cpu_gpu_sync_submitted;
  int deadline_required;
  int deadline_allowed;
  int deadline_armed;
  int deadline_timer_required;
  int deadline_timer_armed;
  int deadline_expired;
  int deadline_completion_required;
  int deadline_completion_reported;
  int deadline_cpu_gpu_sync_allowed;
  int deadline_cpu_gpu_sync_submitted;
  int sync_submitted;
  int sync_wait_allowed;
  int sync_wait_submitted;
  int sync_signal_allowed;
  int sync_signal_submitted;
  int sync_deadline_armed;
  int sync_completion_reported;
  int sync_cpu_gpu_sync_allowed;
  int sync_cpu_gpu_sync_submitted;
  int timeline_submitted;
  int timeline_wait_allowed;
  int timeline_wait_submitted;
  int timeline_signal_allowed;
  int timeline_signal_submitted;
  int timeline_semaphore_allowed;
  int timeline_semaphore_submitted;
  int timeline_value_allocated;
  int timeline_value_published;
  int timeline_cpu_gpu_sync_allowed;
  int timeline_cpu_gpu_sync_submitted;
  int fence_submitted;
  int fence_wait_allowed;
  int fence_wait_submitted;
  int fence_signal_allowed;
  int fence_signal_submitted;
  int fence_fd_export_allowed;
  int fence_fd_exported;
  int fence_cpu_gpu_sync_allowed;
  int fence_cpu_gpu_sync_submitted;
  int barrier_submitted;
  int barrier_memory_visibility_established;
  int barrier_cache_visibility_established;
  int barrier_cpu_gpu_sync_allowed;
  int barrier_cpu_gpu_sync_submitted;
  int flush_submitted;
  int flush_cache_clean_allowed;
  int flush_cache_cleaned;
  int flush_memory_barrier_allowed;
  int flush_memory_barrier_submitted;
  int framebuffer_submitted;
  int framebuffer_mapped;
  int framebuffer_write_allowed;
  int framebuffer_written;
  int framebuffer_flushed;
  int framebuffer_cache_cleaned;
  int blit_submitted;
  int blit_source_buffer_mapped;
  int blit_destination_buffer_mapped;
  int blit_pixels_copied;
  int blit_dma_allowed;
  int blit_dma_submitted;
  int output_submitted;
  int output_buffer_attached;
  int output_buffer_submitted;
  int output_flip_allowed;
  int output_flip_submitted;
  int display_submitted;
  int display_buffer_attached;
  int display_buffer_submitted;
  int display_mode_committed;
  int display_flip_allowed;
  int display_flip_submitted;
  int scanout_submitted;
  int scanout_buffer_attached;
  int scanout_buffer_submitted;
  int vsync_submitted;
  int vsync_wait_submitted;
  int vsync_fence_armed;
  int schedule_submitted;
  int present_submitted;
  int damage_submitted;
  int compositor_damage_submitted;
  int frame_timer_armed;
  int compositor_wake_allowed;
  int compositor_wake_submitted;
  int page_flip_allowed;
  int page_flip_submitted;
  int retire_required;
  int retire_allowed;
  int retire_submitted;
  int retire_ticket_selected;
  int retire_target_selected;
  int retire_resource_release_allowed;
  int retire_resource_released;
  int retire_cpu_gpu_sync_allowed;
  int retire_cpu_gpu_sync_submitted;
  int retire_credential_panel;
  int retire_credential_input;
  int retire_credential_focus;
  int retire_text_recovery;
  int retire_text_login;
  int retire_text_login_resume;
  int retire_text_login_fallback;
  int retire_error;
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
  const char *ack_ticket;
  const char *retire_ticket;
  const char *focus_target;
  const char *primary_action;
  const char *route;
  const char *compositor_target;
  const char *ack_policy;
  const char *retire_policy;
  const char *event_type;
  const char *state;
  const char *message;
  const char *blocked_reason;
};

#endif /* AUTH_LOGIN_RUNTIME_RETIRE_PLAN_H */
