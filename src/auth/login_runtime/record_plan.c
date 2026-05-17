/*
 * src/auth/login_runtime/record_plan.c
 *
 * Credential-screen record plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.35 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the record stage of the credential pipeline:
 *
 *   - login_window_credential_screen_record_plan_reset (static)
 *   - login_window_credential_screen_record_plan_audit_is_safe (static)
 *   - login_window_credential_screen_record_plan_build
 *
 * The record-plan converts a fail-closed audit-plan into a record
 * contract for the downstream receipt stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_audit_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_record_plan_reset(
    struct login_window_credential_screen_record_plan *out,
    int audit_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RECORD_PLAN_VERSION;
  out->audit_plan_available = audit_plan_available ? 1 : 0;
  out->audit_plan_safe = 0;
  out->record_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->audit_required = 1;
  out->audit_allowed = 0;
  out->audit_submitted = 0;
  out->audit_ticket_selected = 0;
  out->audit_target_selected = 0;
  out->audit_log_append_allowed = 0;
  out->audit_log_appended = 0;
  out->audit_cpu_gpu_sync_allowed = 0;
  out->audit_cpu_gpu_sync_submitted = 0;
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
  out->record_required = 1;
  out->record_allowed = 0;
  out->record_submitted = 0;
  out->record_ticket_selected = 0;
  out->record_target_selected = 0;
  out->record_persist_allowed = 0;
  out->record_persisted = 0;
  out->record_cpu_gpu_sync_allowed = 0;
  out->record_cpu_gpu_sync_submitted = 0;
  out->record_credential_panel = 0;
  out->record_credential_input = 0;
  out->record_credential_focus = 0;
  out->record_text_recovery = 0;
  out->record_text_login = 1;
  out->record_text_login_resume = 0;
  out->record_text_login_fallback = 1;
  out->record_error = 1;
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
  out->audit_ticket = "text-login-fallback-audit-ticket";
  out->record_ticket = "text-login-fallback-record-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->audit_policy = "audit-disabled";
  out->record_policy = "record-disabled";
  out->event_type = "credential-screen-record-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "audit-plan-unavailable";
}

static int login_window_credential_screen_record_plan_audit_is_safe(
    const struct login_window_credential_screen_audit_plan *audit_plan) {
  return audit_plan->audit_plan_safe && audit_plan->audit_required &&
         audit_plan->audit_allowed && !audit_plan->audit_submitted &&
         audit_plan->audit_ticket_selected && audit_plan->audit_target_selected &&
         !audit_plan->audit_log_append_allowed && !audit_plan->audit_log_appended &&
         !audit_plan->audit_cpu_gpu_sync_allowed && !audit_plan->audit_cpu_gpu_sync_submitted &&
         !audit_plan->audit_error && audit_plan->seal_required &&
         audit_plan->seal_allowed && !audit_plan->seal_submitted &&
         audit_plan->seal_ticket_selected && audit_plan->seal_target_selected &&
         !audit_plan->seal_state_write_allowed && !audit_plan->seal_state_written &&
         !audit_plan->seal_cpu_gpu_sync_allowed && !audit_plan->seal_cpu_gpu_sync_submitted &&
         audit_plan->cleanup_required && audit_plan->cleanup_allowed &&
         !audit_plan->cleanup_submitted && audit_plan->cleanup_ticket_selected &&
         audit_plan->cleanup_target_selected && !audit_plan->cleanup_resource_release_allowed &&
         !audit_plan->cleanup_resource_released && !audit_plan->cleanup_cpu_gpu_sync_allowed &&
         !audit_plan->cleanup_cpu_gpu_sync_submitted && audit_plan->retire_required &&
         audit_plan->retire_allowed && !audit_plan->retire_submitted &&
         audit_plan->retire_ticket_selected && audit_plan->retire_target_selected &&
         !audit_plan->retire_resource_release_allowed && !audit_plan->retire_resource_released &&
         !audit_plan->retire_cpu_gpu_sync_allowed && !audit_plan->retire_cpu_gpu_sync_submitted &&
         audit_plan->ack_required && audit_plan->ack_allowed && !audit_plan->ack_submitted &&
         audit_plan->ack_ticket_selected && audit_plan->ack_target_selected &&
         !audit_plan->ack_cpu_gpu_sync_allowed && !audit_plan->ack_cpu_gpu_sync_submitted &&
         audit_plan->completion_required && audit_plan->completion_allowed &&
         audit_plan->completion_report_required && !audit_plan->completion_reported &&
         audit_plan->completion_ack_required && !audit_plan->completion_acknowledged &&
         audit_plan->completion_ticket_selected && audit_plan->completion_target_selected &&
         !audit_plan->completion_cpu_gpu_sync_allowed &&
         !audit_plan->completion_cpu_gpu_sync_submitted && audit_plan->deadline_required &&
         audit_plan->deadline_allowed && !audit_plan->deadline_armed &&
         audit_plan->deadline_timer_required && !audit_plan->deadline_timer_armed &&
         !audit_plan->deadline_expired && audit_plan->deadline_completion_required &&
         !audit_plan->deadline_completion_reported && !audit_plan->deadline_cpu_gpu_sync_allowed &&
         !audit_plan->deadline_cpu_gpu_sync_submitted && !audit_plan->sync_submitted &&
         !audit_plan->sync_wait_allowed && !audit_plan->sync_wait_submitted &&
         !audit_plan->sync_signal_allowed && !audit_plan->sync_signal_submitted &&
         !audit_plan->sync_deadline_armed && !audit_plan->sync_completion_reported &&
         !audit_plan->sync_cpu_gpu_sync_allowed && !audit_plan->sync_cpu_gpu_sync_submitted &&
         !audit_plan->timeline_submitted && !audit_plan->timeline_wait_allowed &&
         !audit_plan->timeline_wait_submitted && !audit_plan->timeline_signal_allowed &&
         !audit_plan->timeline_signal_submitted && !audit_plan->timeline_semaphore_allowed &&
         !audit_plan->timeline_semaphore_submitted && !audit_plan->timeline_value_allocated &&
         !audit_plan->timeline_value_published && !audit_plan->timeline_cpu_gpu_sync_allowed &&
         !audit_plan->timeline_cpu_gpu_sync_submitted && !audit_plan->fence_submitted &&
         !audit_plan->fence_wait_allowed && !audit_plan->fence_wait_submitted &&
         !audit_plan->fence_signal_allowed && !audit_plan->fence_signal_submitted &&
         !audit_plan->fence_fd_export_allowed && !audit_plan->fence_fd_exported &&
         !audit_plan->fence_cpu_gpu_sync_allowed && !audit_plan->fence_cpu_gpu_sync_submitted &&
         !audit_plan->barrier_submitted && !audit_plan->barrier_memory_visibility_established &&
         !audit_plan->barrier_cache_visibility_established && !audit_plan->barrier_cpu_gpu_sync_allowed &&
         !audit_plan->barrier_cpu_gpu_sync_submitted && !audit_plan->flush_submitted &&
         !audit_plan->flush_cache_clean_allowed && !audit_plan->flush_cache_cleaned &&
         !audit_plan->flush_memory_barrier_allowed && !audit_plan->flush_memory_barrier_submitted &&
         !audit_plan->framebuffer_submitted && !audit_plan->framebuffer_mapped &&
         !audit_plan->framebuffer_write_allowed && !audit_plan->framebuffer_written &&
         !audit_plan->framebuffer_flushed && !audit_plan->framebuffer_cache_cleaned &&
         !audit_plan->blit_submitted && !audit_plan->blit_source_buffer_mapped &&
         !audit_plan->blit_destination_buffer_mapped && !audit_plan->blit_pixels_copied &&
         !audit_plan->blit_dma_allowed && !audit_plan->blit_dma_submitted &&
         !audit_plan->output_submitted && !audit_plan->output_buffer_attached &&
         !audit_plan->output_buffer_submitted && !audit_plan->output_flip_allowed &&
         !audit_plan->output_flip_submitted && !audit_plan->display_submitted &&
         !audit_plan->display_buffer_attached && !audit_plan->display_buffer_submitted &&
         !audit_plan->display_mode_committed && !audit_plan->display_flip_allowed &&
         !audit_plan->display_flip_submitted && !audit_plan->scanout_submitted &&
         !audit_plan->scanout_buffer_attached && !audit_plan->scanout_buffer_submitted &&
         !audit_plan->vsync_submitted && !audit_plan->vsync_wait_submitted &&
         !audit_plan->vsync_fence_armed && !audit_plan->schedule_submitted &&
         !audit_plan->present_submitted && !audit_plan->damage_submitted &&
         !audit_plan->compositor_damage_submitted && !audit_plan->frame_timer_armed &&
         !audit_plan->compositor_wake_allowed && !audit_plan->compositor_wake_submitted &&
         !audit_plan->page_flip_allowed && !audit_plan->page_flip_submitted &&
         audit_plan->route_selected && !audit_plan->route_blocked &&
         audit_plan->credential_session_safe && audit_plan->credential_storage_wiped &&
         audit_plan->credential_redacted && audit_plan->length_redacted &&
         !audit_plan->raw_secret_exposed && !audit_plan->masked_text_exposed &&
         audit_plan->submit_blocked && !audit_plan->submit_enabled &&
         !audit_plan->auth_attempt_allowed && !audit_plan->submit_callback_bound &&
         !audit_plan->auth_callback_bound && audit_plan->text_login_authoritative;
}

int login_window_credential_screen_record_plan_build(
    const struct login_window_credential_screen_audit_plan *audit_plan,
    struct login_window_credential_screen_record_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_record_plan_reset(out, audit_plan ? 1 : 0);
  if (!audit_plan) return 0;
  out->requested_action = audit_plan->requested_action;
  out->audit_plan_safe = audit_plan->audit_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_record_plan_audit_is_safe(audit_plan)) {
    out->event_type = "credential-screen-record-plan-unsafe";
    out->blocked_reason = "credential-record-plan-unsafe";
    out->message = "Credential screen record plan unsafe; use text login.";
    return 0;
  }
  out->record_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = audit_plan->action_allowed ? 1 : 0;
  out->action_blocked = audit_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = audit_plan->input_focus_allowed ? 1 : 0;
  out->audit_required = audit_plan->audit_required ? 1 : 0;
  out->audit_allowed = audit_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = audit_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = audit_plan->audit_target_selected ? 1 : 0;
  out->seal_required = audit_plan->seal_required ? 1 : 0;
  out->seal_allowed = audit_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = audit_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = audit_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = audit_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = audit_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = audit_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = audit_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = audit_plan->retire_required ? 1 : 0;
  out->retire_allowed = audit_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = audit_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = audit_plan->retire_target_selected ? 1 : 0;
  out->ack_required = audit_plan->ack_required ? 1 : 0;
  out->ack_allowed = audit_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = audit_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = audit_plan->ack_target_selected ? 1 : 0;
  out->completion_required = audit_plan->completion_required ? 1 : 0;
  out->completion_allowed = audit_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = audit_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = audit_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = audit_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = audit_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = audit_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = audit_plan->deadline_allowed ? 1 : 0;
  out->record_allowed = 1;
  out->record_ticket_selected = 1;
  out->record_target_selected = 1;
  out->record_error = 0;
  out->recovery_text_session_required = audit_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = audit_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = audit_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = audit_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = audit_plan->view;
  out->widget_tree = audit_plan->widget_tree;
  out->audit_ticket = audit_plan->audit_ticket;
  out->focus_target = audit_plan->focus_target;
  out->primary_action = audit_plan->primary_action;
  out->route = audit_plan->route;
  out->compositor_target = audit_plan->compositor_target;
  out->audit_policy = audit_plan->audit_policy;
  out->record_policy = "declarative-record-no-persist";
  out->event_type = "credential-screen-record-plan-ready";
  out->state = "record-ready";
  out->message = "Credential screen record ticket ready; no record persisted.";
  out->blocked_reason = audit_plan->blocked_reason;
  if (audit_plan->submit_requested) {
    out->record_ticket = "text-login-fallback-record-ticket";
    out->compositor_target = "text-login-fallback-record";
    out->record_policy = "fallback-record-declarative";
    out->record_text_login = 1;
    out->record_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "record-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (audit_plan->audit_credential_panel && audit_plan->audit_credential_input &&
      audit_plan->audit_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->record_ticket = "credential-screen-record-ticket";
    out->compositor_target = "credential-screen-record";
    out->record_credential_panel = 1;
    out->record_credential_input = 1;
    out->record_credential_focus = 1;
    out->record_text_login = 0;
    out->record_text_login_fallback = 0;
    out->state = "record-credential-ready";
    out->message = "Credential record ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (audit_plan->audit_text_recovery && out->recovery_text_session_required) {
    out->record_ticket = "text-recovery-record-ticket";
    out->compositor_target = "text-recovery-record";
    out->record_text_recovery = 1;
    out->record_text_login = 1;
    out->record_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "record-text-recovery-ready";
    out->message = "Text recovery record ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (audit_plan->audit_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->record_ticket = "text-login-resume-record-ticket";
    out->compositor_target = "text-login-resume-record";
    out->record_policy = "full-record-declarative";
    out->record_text_login = 1;
    out->record_text_login_resume = 1;
    out->record_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "record-resume-ready";
    out->message = "Text login resume record ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->record_ticket = "text-login-fallback-record-ticket";
  out->compositor_target = "text-login-fallback-record";
  out->record_policy = "fallback-record-declarative";
  out->record_text_login = 1;
  out->record_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "record-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
