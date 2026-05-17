/*
 * src/auth/login_runtime/audit_plan.c
 *
 * Credential-screen audit plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.34 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the audit stage of the credential pipeline:
 *
 *   - login_window_credential_screen_audit_plan_reset (static)
 *   - login_window_credential_screen_audit_plan_seal_is_safe (static)
 *   - login_window_credential_screen_audit_plan_build
 *
 * The audit-plan converts a fail-closed seal-plan into an audit
 * contract for the downstream record stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_seal_is_safe` predicate consolidates the upstream-safety checks
 * shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_audit_plan_reset(
    struct login_window_credential_screen_audit_plan *out,
    int seal_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_AUDIT_PLAN_VERSION;
  out->seal_plan_available = seal_plan_available ? 1 : 0;
  out->seal_plan_safe = 0;
  out->audit_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->seal_required = 1;
  out->seal_allowed = 0;
  out->seal_submitted = 0;
  out->seal_ticket_selected = 0;
  out->seal_target_selected = 0;
  out->seal_state_write_allowed = 0;
  out->seal_state_written = 0;
  out->seal_cpu_gpu_sync_allowed = 0;
  out->seal_cpu_gpu_sync_submitted = 0;
  out->cleanup_required = 1;
  out->cleanup_allowed = 0;
  out->cleanup_submitted = 0;
  out->cleanup_ticket_selected = 0;
  out->cleanup_target_selected = 0;
  out->cleanup_resource_release_allowed = 0;
  out->cleanup_resource_released = 0;
  out->cleanup_cpu_gpu_sync_allowed = 0;
  out->cleanup_cpu_gpu_sync_submitted = 0;
  out->retire_required = 1;
  out->retire_allowed = 0;
  out->retire_submitted = 0;
  out->retire_ticket_selected = 0;
  out->retire_target_selected = 0;
  out->retire_resource_release_allowed = 0;
  out->retire_resource_released = 0;
  out->retire_cpu_gpu_sync_allowed = 0;
  out->retire_cpu_gpu_sync_submitted = 0;
  out->ack_required = 1;
  out->ack_allowed = 0;
  out->ack_submitted = 0;
  out->ack_ticket_selected = 0;
  out->ack_target_selected = 0;
  out->ack_cpu_gpu_sync_allowed = 0;
  out->ack_cpu_gpu_sync_submitted = 0;
  out->completion_required = 1;
  out->completion_allowed = 0;
  out->completion_report_required = 1;
  out->completion_reported = 0;
  out->completion_ack_required = 1;
  out->completion_acknowledged = 0;
  out->completion_ticket_selected = 0;
  out->completion_target_selected = 0;
  out->completion_cpu_gpu_sync_allowed = 0;
  out->completion_cpu_gpu_sync_submitted = 0;
  out->deadline_required = 1;
  out->deadline_allowed = 0;
  out->deadline_armed = 0;
  out->deadline_timer_required = 1;
  out->deadline_timer_armed = 0;
  out->deadline_expired = 0;
  out->deadline_completion_required = 1;
  out->deadline_completion_reported = 0;
  out->deadline_cpu_gpu_sync_allowed = 0;
  out->deadline_cpu_gpu_sync_submitted = 0;
  out->sync_submitted = 0;
  out->sync_wait_allowed = 0;
  out->sync_wait_submitted = 0;
  out->sync_signal_allowed = 0;
  out->sync_signal_submitted = 0;
  out->sync_deadline_armed = 0;
  out->sync_completion_reported = 0;
  out->sync_cpu_gpu_sync_allowed = 0;
  out->sync_cpu_gpu_sync_submitted = 0;
  out->timeline_submitted = 0;
  out->timeline_wait_allowed = 0;
  out->timeline_wait_submitted = 0;
  out->timeline_signal_allowed = 0;
  out->timeline_signal_submitted = 0;
  out->timeline_semaphore_allowed = 0;
  out->timeline_semaphore_submitted = 0;
  out->timeline_value_allocated = 0;
  out->timeline_value_published = 0;
  out->timeline_cpu_gpu_sync_allowed = 0;
  out->timeline_cpu_gpu_sync_submitted = 0;
  out->fence_submitted = 0;
  out->fence_wait_allowed = 0;
  out->fence_wait_submitted = 0;
  out->fence_signal_allowed = 0;
  out->fence_signal_submitted = 0;
  out->fence_fd_export_allowed = 0;
  out->fence_fd_exported = 0;
  out->fence_cpu_gpu_sync_allowed = 0;
  out->fence_cpu_gpu_sync_submitted = 0;
  out->barrier_submitted = 0;
  out->barrier_memory_visibility_established = 0;
  out->barrier_cache_visibility_established = 0;
  out->barrier_cpu_gpu_sync_allowed = 0;
  out->barrier_cpu_gpu_sync_submitted = 0;
  out->flush_submitted = 0;
  out->flush_cache_clean_allowed = 0;
  out->flush_cache_cleaned = 0;
  out->flush_memory_barrier_allowed = 0;
  out->flush_memory_barrier_submitted = 0;
  out->framebuffer_submitted = 0;
  out->framebuffer_mapped = 0;
  out->framebuffer_write_allowed = 0;
  out->framebuffer_written = 0;
  out->framebuffer_flushed = 0;
  out->framebuffer_cache_cleaned = 0;
  out->blit_submitted = 0;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->output_submitted = 0;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->display_submitted = 0;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->scanout_submitted = 0;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->vsync_submitted = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_armed = 0;
  out->schedule_submitted = 0;
  out->present_submitted = 0;
  out->damage_submitted = 0;
  out->compositor_damage_submitted = 0;
  out->frame_timer_armed = 0;
  out->compositor_wake_allowed = 0;
  out->compositor_wake_submitted = 0;
  out->page_flip_allowed = 0;
  out->page_flip_submitted = 0;
  out->audit_required = 1;
  out->audit_allowed = 0;
  out->audit_submitted = 0;
  out->audit_ticket_selected = 0;
  out->audit_target_selected = 0;
  out->audit_log_append_allowed = 0;
  out->audit_log_appended = 0;
  out->audit_cpu_gpu_sync_allowed = 0;
  out->audit_cpu_gpu_sync_submitted = 0;
  out->audit_credential_panel = 0;
  out->audit_credential_input = 0;
  out->audit_credential_focus = 0;
  out->audit_text_recovery = 0;
  out->audit_text_login = 1;
  out->audit_text_login_resume = 0;
  out->audit_text_login_fallback = 1;
  out->audit_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_session_safe = 0;
  out->credential_storage_wiped = 0;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->raw_secret_exposed = 0;
  out->masked_text_exposed = 0;
  out->submit_requested = 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->seal_ticket = "text-login-fallback-seal-ticket";
  out->audit_ticket = "text-login-fallback-audit-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->seal_policy = "seal-disabled";
  out->audit_policy = "audit-disabled";
  out->event_type = "credential-screen-audit-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "seal-plan-unavailable";
}

static int login_window_credential_screen_audit_plan_seal_is_safe(
    const struct login_window_credential_screen_seal_plan *seal_plan) {
  return seal_plan->seal_plan_safe && seal_plan->seal_required &&
         seal_plan->seal_allowed && !seal_plan->seal_submitted &&
         seal_plan->seal_ticket_selected && seal_plan->seal_target_selected &&
         !seal_plan->seal_state_write_allowed && !seal_plan->seal_state_written &&
         !seal_plan->seal_cpu_gpu_sync_allowed && !seal_plan->seal_cpu_gpu_sync_submitted &&
         !seal_plan->seal_error && seal_plan->cleanup_required &&
         seal_plan->cleanup_allowed && !seal_plan->cleanup_submitted &&
         seal_plan->cleanup_ticket_selected && seal_plan->cleanup_target_selected &&
         !seal_plan->cleanup_resource_release_allowed &&
         !seal_plan->cleanup_resource_released && !seal_plan->cleanup_cpu_gpu_sync_allowed &&
         !seal_plan->cleanup_cpu_gpu_sync_submitted && seal_plan->retire_required &&
         seal_plan->retire_allowed && !seal_plan->retire_submitted &&
         seal_plan->retire_ticket_selected && seal_plan->retire_target_selected &&
         !seal_plan->retire_resource_release_allowed && !seal_plan->retire_resource_released &&
         !seal_plan->retire_cpu_gpu_sync_allowed && !seal_plan->retire_cpu_gpu_sync_submitted &&
         seal_plan->ack_required && seal_plan->ack_allowed && !seal_plan->ack_submitted &&
         seal_plan->ack_ticket_selected && seal_plan->ack_target_selected &&
         !seal_plan->ack_cpu_gpu_sync_allowed && !seal_plan->ack_cpu_gpu_sync_submitted &&
         seal_plan->completion_required && seal_plan->completion_allowed &&
         seal_plan->completion_report_required && !seal_plan->completion_reported &&
         seal_plan->completion_ack_required && !seal_plan->completion_acknowledged &&
         seal_plan->completion_ticket_selected && seal_plan->completion_target_selected &&
         !seal_plan->completion_cpu_gpu_sync_allowed &&
         !seal_plan->completion_cpu_gpu_sync_submitted && seal_plan->deadline_required &&
         seal_plan->deadline_allowed && !seal_plan->deadline_armed &&
         seal_plan->deadline_timer_required && !seal_plan->deadline_timer_armed &&
         !seal_plan->deadline_expired && seal_plan->deadline_completion_required &&
         !seal_plan->deadline_completion_reported && !seal_plan->deadline_cpu_gpu_sync_allowed &&
         !seal_plan->deadline_cpu_gpu_sync_submitted && !seal_plan->sync_submitted &&
         !seal_plan->sync_wait_allowed && !seal_plan->sync_wait_submitted &&
         !seal_plan->sync_signal_allowed && !seal_plan->sync_signal_submitted &&
         !seal_plan->sync_deadline_armed && !seal_plan->sync_completion_reported &&
         !seal_plan->sync_cpu_gpu_sync_allowed && !seal_plan->sync_cpu_gpu_sync_submitted &&
         !seal_plan->timeline_submitted && !seal_plan->timeline_wait_allowed &&
         !seal_plan->timeline_wait_submitted && !seal_plan->timeline_signal_allowed &&
         !seal_plan->timeline_signal_submitted && !seal_plan->timeline_semaphore_allowed &&
         !seal_plan->timeline_semaphore_submitted && !seal_plan->timeline_value_allocated &&
         !seal_plan->timeline_value_published && !seal_plan->timeline_cpu_gpu_sync_allowed &&
         !seal_plan->timeline_cpu_gpu_sync_submitted && !seal_plan->fence_submitted &&
         !seal_plan->fence_wait_allowed && !seal_plan->fence_wait_submitted &&
         !seal_plan->fence_signal_allowed && !seal_plan->fence_signal_submitted &&
         !seal_plan->fence_fd_export_allowed && !seal_plan->fence_fd_exported &&
         !seal_plan->fence_cpu_gpu_sync_allowed && !seal_plan->fence_cpu_gpu_sync_submitted &&
         !seal_plan->barrier_submitted && !seal_plan->barrier_memory_visibility_established &&
         !seal_plan->barrier_cache_visibility_established && !seal_plan->barrier_cpu_gpu_sync_allowed &&
         !seal_plan->barrier_cpu_gpu_sync_submitted && !seal_plan->flush_submitted &&
         !seal_plan->flush_cache_clean_allowed && !seal_plan->flush_cache_cleaned &&
         !seal_plan->flush_memory_barrier_allowed && !seal_plan->flush_memory_barrier_submitted &&
         !seal_plan->framebuffer_submitted && !seal_plan->framebuffer_mapped &&
         !seal_plan->framebuffer_write_allowed && !seal_plan->framebuffer_written &&
         !seal_plan->framebuffer_flushed && !seal_plan->framebuffer_cache_cleaned &&
         !seal_plan->blit_submitted && !seal_plan->blit_source_buffer_mapped &&
         !seal_plan->blit_destination_buffer_mapped && !seal_plan->blit_pixels_copied &&
         !seal_plan->blit_dma_allowed && !seal_plan->blit_dma_submitted &&
         !seal_plan->output_submitted && !seal_plan->output_buffer_attached &&
         !seal_plan->output_buffer_submitted && !seal_plan->output_flip_allowed &&
         !seal_plan->output_flip_submitted && !seal_plan->display_submitted &&
         !seal_plan->display_buffer_attached && !seal_plan->display_buffer_submitted &&
         !seal_plan->display_mode_committed && !seal_plan->display_flip_allowed &&
         !seal_plan->display_flip_submitted && !seal_plan->scanout_submitted &&
         !seal_plan->scanout_buffer_attached && !seal_plan->scanout_buffer_submitted &&
         !seal_plan->vsync_submitted && !seal_plan->vsync_wait_submitted &&
         !seal_plan->vsync_fence_armed && !seal_plan->schedule_submitted &&
         !seal_plan->present_submitted && !seal_plan->damage_submitted &&
         !seal_plan->compositor_damage_submitted && !seal_plan->frame_timer_armed &&
         !seal_plan->compositor_wake_allowed && !seal_plan->compositor_wake_submitted &&
         !seal_plan->page_flip_allowed && !seal_plan->page_flip_submitted &&
         seal_plan->route_selected && !seal_plan->route_blocked &&
         seal_plan->credential_session_safe && seal_plan->credential_storage_wiped &&
         seal_plan->credential_redacted && seal_plan->length_redacted &&
         !seal_plan->raw_secret_exposed && !seal_plan->masked_text_exposed &&
         seal_plan->submit_blocked && !seal_plan->submit_enabled &&
         !seal_plan->auth_attempt_allowed && !seal_plan->submit_callback_bound &&
         !seal_plan->auth_callback_bound && seal_plan->text_login_authoritative;
}

int login_window_credential_screen_audit_plan_build(
    const struct login_window_credential_screen_seal_plan *seal_plan,
    struct login_window_credential_screen_audit_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_audit_plan_reset(out, seal_plan ? 1 : 0);
  if (!seal_plan) return 0;
  out->requested_action = seal_plan->requested_action;
  out->seal_plan_safe = seal_plan->seal_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_audit_plan_seal_is_safe(seal_plan)) {
    out->event_type = "credential-screen-audit-plan-unsafe";
    out->blocked_reason = "credential-audit-plan-unsafe";
    out->message = "Credential screen audit plan unsafe; use text login.";
    return 0;
  }
  out->audit_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = seal_plan->action_allowed ? 1 : 0;
  out->action_blocked = seal_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = seal_plan->input_focus_allowed ? 1 : 0;
  out->seal_required = seal_plan->seal_required ? 1 : 0;
  out->seal_allowed = seal_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = seal_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = seal_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = seal_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = seal_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = seal_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = seal_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = seal_plan->retire_required ? 1 : 0;
  out->retire_allowed = seal_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = seal_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = seal_plan->retire_target_selected ? 1 : 0;
  out->ack_required = seal_plan->ack_required ? 1 : 0;
  out->ack_allowed = seal_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = seal_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = seal_plan->ack_target_selected ? 1 : 0;
  out->completion_required = seal_plan->completion_required ? 1 : 0;
  out->completion_allowed = seal_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = seal_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = seal_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = seal_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = seal_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = seal_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = seal_plan->deadline_allowed ? 1 : 0;
  out->audit_allowed = 1;
  out->audit_ticket_selected = 1;
  out->audit_target_selected = 1;
  out->audit_error = 0;
  out->recovery_text_session_required = seal_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = seal_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = seal_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = seal_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = seal_plan->view;
  out->widget_tree = seal_plan->widget_tree;
  out->seal_ticket = seal_plan->seal_ticket;
  out->focus_target = seal_plan->focus_target;
  out->primary_action = seal_plan->primary_action;
  out->route = seal_plan->route;
  out->compositor_target = seal_plan->compositor_target;
  out->seal_policy = seal_plan->seal_policy;
  out->audit_policy = "declarative-audit-no-log-append";
  out->event_type = "credential-screen-audit-plan-ready";
  out->state = "audit-ready";
  out->message = "Credential screen audit ticket ready; no log appended.";
  out->blocked_reason = seal_plan->blocked_reason;
  if (seal_plan->submit_requested) {
    out->audit_ticket = "text-login-fallback-audit-ticket";
    out->compositor_target = "text-login-fallback-audit";
    out->audit_policy = "fallback-audit-declarative";
    out->audit_text_login = 1;
    out->audit_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "audit-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (seal_plan->seal_credential_panel && seal_plan->seal_credential_input &&
      seal_plan->seal_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->audit_ticket = "credential-screen-audit-ticket";
    out->compositor_target = "credential-screen-audit";
    out->audit_credential_panel = 1;
    out->audit_credential_input = 1;
    out->audit_credential_focus = 1;
    out->audit_text_login = 0;
    out->audit_text_login_fallback = 0;
    out->state = "audit-credential-ready";
    out->message = "Credential audit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (seal_plan->seal_text_recovery && out->recovery_text_session_required) {
    out->audit_ticket = "text-recovery-audit-ticket";
    out->compositor_target = "text-recovery-audit";
    out->audit_text_recovery = 1;
    out->audit_text_login = 1;
    out->audit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "audit-text-recovery-ready";
    out->message = "Text recovery audit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (seal_plan->seal_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->audit_ticket = "text-login-resume-audit-ticket";
    out->compositor_target = "text-login-resume-audit";
    out->audit_policy = "full-audit-declarative";
    out->audit_text_login = 1;
    out->audit_text_login_resume = 1;
    out->audit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "audit-resume-ready";
    out->message = "Text login resume audit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->audit_ticket = "text-login-fallback-audit-ticket";
  out->compositor_target = "text-login-fallback-audit";
  out->audit_policy = "fallback-audit-declarative";
  out->audit_text_login = 1;
  out->audit_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "audit-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
