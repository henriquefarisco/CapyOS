/*
 * src/auth/login_runtime/receipt_plan.c
 *
 * Credential-screen receipt plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.36 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the receipt stage of the credential pipeline:
 *
 *   - login_window_credential_screen_receipt_plan_reset (static)
 *   - login_window_credential_screen_receipt_plan_record_is_safe (static)
 *   - login_window_credential_screen_receipt_plan_build
 *
 * The receipt-plan converts a fail-closed record-plan into a
 * receipt contract for the downstream ledger stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_record_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_receipt_plan_reset(
    struct login_window_credential_screen_receipt_plan *out,
    int record_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RECEIPT_PLAN_VERSION;
  out->record_plan_available = record_plan_available ? 1 : 0;
  out->record_plan_safe = 0;
  out->receipt_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->record_required = 1;
  out->record_allowed = 0;
  out->record_submitted = 0;
  out->record_ticket_selected = 0;
  out->record_target_selected = 0;
  out->record_persist_allowed = 0;
  out->record_persisted = 0;
  out->record_cpu_gpu_sync_allowed = 0;
  out->record_cpu_gpu_sync_submitted = 0;
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
  out->receipt_required = 1;
  out->receipt_allowed = 0;
  out->receipt_submitted = 0;
  out->receipt_ticket_selected = 0;
  out->receipt_target_selected = 0;
  out->receipt_persist_allowed = 0;
  out->receipt_persisted = 0;
  out->receipt_cpu_gpu_sync_allowed = 0;
  out->receipt_cpu_gpu_sync_submitted = 0;
  out->record_credential_panel = 0;
  out->record_credential_input = 0;
  out->record_credential_focus = 0;
  out->record_text_recovery = 0;
  out->record_text_login = 1;
  out->record_text_login_resume = 0;
  out->record_text_login_fallback = 1;
  out->record_error = 1;
  out->receipt_credential_panel = 0;
  out->receipt_credential_input = 0;
  out->receipt_credential_focus = 0;
  out->receipt_text_recovery = 0;
  out->receipt_text_login = 1;
  out->receipt_text_login_resume = 0;
  out->receipt_text_login_fallback = 1;
  out->receipt_error = 1;
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
  out->record_ticket = "text-login-fallback-record-ticket";
  out->receipt_ticket = "text-login-fallback-receipt-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->record_policy = "record-disabled";
  out->receipt_policy = "receipt-disabled";
  out->event_type = "credential-screen-receipt-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "record-plan-unavailable";
}

static int login_window_credential_screen_receipt_plan_record_is_safe(
    const struct login_window_credential_screen_record_plan *record_plan) {
  return record_plan->record_plan_safe &&
         record_plan->record_required &&
         record_plan->record_allowed &&
         !record_plan->record_submitted &&
         record_plan->record_ticket_selected &&
         record_plan->record_target_selected &&
         !record_plan->record_persist_allowed &&
         !record_plan->record_persisted &&
         !record_plan->record_cpu_gpu_sync_allowed &&
         !record_plan->record_cpu_gpu_sync_submitted &&
         !record_plan->record_error &&
         record_plan->audit_required &&
         record_plan->audit_allowed &&
         !record_plan->audit_submitted &&
         record_plan->audit_ticket_selected &&
         record_plan->audit_target_selected &&
         !record_plan->audit_log_append_allowed &&
         !record_plan->audit_log_appended &&
         !record_plan->audit_cpu_gpu_sync_allowed &&
         !record_plan->audit_cpu_gpu_sync_submitted &&
         record_plan->seal_required &&
         record_plan->seal_allowed &&
         !record_plan->seal_submitted &&
         record_plan->seal_ticket_selected &&
         record_plan->seal_target_selected &&
         !record_plan->seal_state_write_allowed &&
         !record_plan->seal_state_written &&
         !record_plan->seal_cpu_gpu_sync_allowed &&
         !record_plan->seal_cpu_gpu_sync_submitted &&
         record_plan->cleanup_required &&
         record_plan->cleanup_allowed &&
         !record_plan->cleanup_submitted &&
         record_plan->cleanup_ticket_selected &&
         record_plan->cleanup_target_selected &&
         !record_plan->cleanup_resource_release_allowed &&
         !record_plan->cleanup_resource_released &&
         !record_plan->cleanup_cpu_gpu_sync_allowed &&
         !record_plan->cleanup_cpu_gpu_sync_submitted &&
         record_plan->retire_required &&
         record_plan->retire_allowed &&
         !record_plan->retire_submitted &&
         record_plan->retire_ticket_selected &&
         record_plan->retire_target_selected &&
         !record_plan->retire_resource_release_allowed &&
         !record_plan->retire_resource_released &&
         !record_plan->retire_cpu_gpu_sync_allowed &&
         !record_plan->retire_cpu_gpu_sync_submitted &&
         record_plan->ack_required &&
         record_plan->ack_allowed &&
         !record_plan->ack_submitted &&
         record_plan->ack_ticket_selected &&
         record_plan->ack_target_selected &&
         !record_plan->ack_cpu_gpu_sync_allowed &&
         !record_plan->ack_cpu_gpu_sync_submitted &&
         record_plan->completion_required &&
         record_plan->completion_allowed &&
         record_plan->completion_report_required &&
         !record_plan->completion_reported &&
         record_plan->completion_ack_required &&
         !record_plan->completion_acknowledged &&
         record_plan->completion_ticket_selected &&
         record_plan->completion_target_selected &&
         !record_plan->completion_cpu_gpu_sync_allowed &&
         !record_plan->completion_cpu_gpu_sync_submitted &&
         record_plan->deadline_required &&
         record_plan->deadline_allowed &&
         !record_plan->deadline_armed &&
         record_plan->deadline_timer_required &&
         !record_plan->deadline_timer_armed &&
         !record_plan->deadline_expired &&
         record_plan->deadline_completion_required &&
         !record_plan->deadline_completion_reported &&
         !record_plan->deadline_cpu_gpu_sync_allowed &&
         !record_plan->deadline_cpu_gpu_sync_submitted &&
         !record_plan->sync_submitted &&
         !record_plan->sync_wait_allowed &&
         !record_plan->sync_wait_submitted &&
         !record_plan->sync_signal_allowed &&
         !record_plan->sync_signal_submitted &&
         !record_plan->sync_deadline_armed &&
         !record_plan->sync_completion_reported &&
         !record_plan->sync_cpu_gpu_sync_allowed &&
         !record_plan->sync_cpu_gpu_sync_submitted &&
         !record_plan->timeline_submitted &&
         !record_plan->timeline_wait_allowed &&
         !record_plan->timeline_wait_submitted &&
         !record_plan->timeline_signal_allowed &&
         !record_plan->timeline_signal_submitted &&
         !record_plan->timeline_semaphore_allowed &&
         !record_plan->timeline_semaphore_submitted &&
         !record_plan->timeline_value_allocated &&
         !record_plan->timeline_value_published &&
         !record_plan->timeline_cpu_gpu_sync_allowed &&
         !record_plan->timeline_cpu_gpu_sync_submitted &&
         !record_plan->fence_submitted &&
         !record_plan->fence_wait_allowed &&
         !record_plan->fence_wait_submitted &&
         !record_plan->fence_signal_allowed &&
         !record_plan->fence_signal_submitted &&
         !record_plan->fence_fd_export_allowed &&
         !record_plan->fence_fd_exported &&
         !record_plan->fence_cpu_gpu_sync_allowed &&
         !record_plan->fence_cpu_gpu_sync_submitted &&
         !record_plan->barrier_submitted &&
         !record_plan->barrier_memory_visibility_established &&
         !record_plan->barrier_cache_visibility_established &&
         !record_plan->barrier_cpu_gpu_sync_allowed &&
         !record_plan->barrier_cpu_gpu_sync_submitted &&
         !record_plan->flush_submitted &&
         !record_plan->flush_cache_clean_allowed &&
         !record_plan->flush_cache_cleaned &&
         !record_plan->flush_memory_barrier_allowed &&
         !record_plan->flush_memory_barrier_submitted &&
         !record_plan->framebuffer_submitted &&
         !record_plan->framebuffer_mapped &&
         !record_plan->framebuffer_write_allowed &&
         !record_plan->framebuffer_written &&
         !record_plan->framebuffer_flushed &&
         !record_plan->framebuffer_cache_cleaned &&
         !record_plan->blit_submitted &&
         !record_plan->blit_source_buffer_mapped &&
         !record_plan->blit_destination_buffer_mapped &&
         !record_plan->blit_pixels_copied &&
         !record_plan->blit_dma_allowed &&
         !record_plan->blit_dma_submitted &&
         !record_plan->output_submitted &&
         !record_plan->output_buffer_attached &&
         !record_plan->output_buffer_submitted &&
         !record_plan->output_flip_allowed &&
         !record_plan->output_flip_submitted &&
         !record_plan->display_submitted &&
         !record_plan->display_buffer_attached &&
         !record_plan->display_buffer_submitted &&
         !record_plan->display_mode_committed &&
         !record_plan->display_flip_allowed &&
         !record_plan->display_flip_submitted &&
         !record_plan->scanout_submitted &&
         !record_plan->scanout_buffer_attached &&
         !record_plan->scanout_buffer_submitted &&
         !record_plan->vsync_submitted &&
         !record_plan->vsync_wait_submitted &&
         !record_plan->vsync_fence_armed &&
         !record_plan->schedule_submitted &&
         !record_plan->present_submitted &&
         !record_plan->damage_submitted &&
         !record_plan->compositor_damage_submitted &&
         !record_plan->frame_timer_armed &&
         !record_plan->compositor_wake_allowed &&
         !record_plan->compositor_wake_submitted &&
         !record_plan->page_flip_allowed &&
         !record_plan->page_flip_submitted &&
         record_plan->route_selected &&
         !record_plan->route_blocked &&
         record_plan->credential_session_safe &&
         record_plan->credential_storage_wiped &&
         record_plan->credential_redacted &&
         record_plan->length_redacted &&
         !record_plan->raw_secret_exposed &&
         !record_plan->masked_text_exposed &&
         record_plan->submit_blocked &&
         !record_plan->submit_enabled &&
         !record_plan->auth_attempt_allowed &&
         !record_plan->submit_callback_bound &&
         !record_plan->auth_callback_bound &&
         record_plan->text_login_authoritative;
}

int login_window_credential_screen_receipt_plan_build(
    const struct login_window_credential_screen_record_plan *record_plan,
    struct login_window_credential_screen_receipt_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_receipt_plan_reset(out, record_plan ? 1 : 0);
  if (!record_plan) return 0;
  out->requested_action = record_plan->requested_action;
  out->record_plan_safe = record_plan->record_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_receipt_plan_record_is_safe(record_plan)) {
    out->event_type = "credential-screen-receipt-plan-unsafe";
    out->blocked_reason = "credential-receipt-plan-unsafe";
    out->message = "Credential screen receipt plan unsafe; use text login.";
    return 0;
  }
  out->receipt_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = record_plan->action_allowed ? 1 : 0;
  out->action_blocked = record_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = record_plan->input_focus_allowed ? 1 : 0;
  out->record_required = record_plan->record_required ? 1 : 0;
  out->record_allowed = record_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = record_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = record_plan->record_target_selected ? 1 : 0;
  out->audit_required = record_plan->audit_required ? 1 : 0;
  out->audit_allowed = record_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = record_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = record_plan->audit_target_selected ? 1 : 0;
  out->seal_required = record_plan->seal_required ? 1 : 0;
  out->seal_allowed = record_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = record_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = record_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = record_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = record_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = record_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = record_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = record_plan->retire_required ? 1 : 0;
  out->retire_allowed = record_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = record_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = record_plan->retire_target_selected ? 1 : 0;
  out->ack_required = record_plan->ack_required ? 1 : 0;
  out->ack_allowed = record_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = record_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = record_plan->ack_target_selected ? 1 : 0;
  out->completion_required = record_plan->completion_required ? 1 : 0;
  out->completion_allowed = record_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = record_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = record_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = record_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = record_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = record_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = record_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = record_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = record_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = record_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = record_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = record_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = record_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = record_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_allowed = 1;
  out->receipt_ticket_selected = 1;
  out->receipt_target_selected = 1;
  out->receipt_error = 0;
  out->recovery_text_session_required = record_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = record_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = record_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = record_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = record_plan->view;
  out->widget_tree = record_plan->widget_tree;
  out->record_ticket = record_plan->record_ticket;
  out->focus_target = record_plan->focus_target;
  out->primary_action = record_plan->primary_action;
  out->route = record_plan->route;
  out->compositor_target = record_plan->compositor_target;
  out->record_policy = record_plan->record_policy;
  out->receipt_policy = "declarative-receipt-no-persist";
  out->event_type = "credential-screen-receipt-plan-ready";
  out->state = "receipt-ready";
  out->message = "Credential screen receipt ticket ready; no receipt persisted.";
  out->blocked_reason = record_plan->blocked_reason;
  if (record_plan->submit_requested) {
    out->receipt_ticket = "text-login-fallback-receipt-ticket";
    out->compositor_target = "text-login-fallback-receipt";
    out->receipt_policy = "fallback-receipt-declarative";
    out->receipt_text_login = 1;
    out->receipt_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "receipt-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (record_plan->record_credential_panel && record_plan->record_credential_input &&
      record_plan->record_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->receipt_ticket = "credential-screen-receipt-ticket";
    out->compositor_target = "credential-screen-receipt";
    out->receipt_credential_panel = 1;
    out->receipt_credential_input = 1;
    out->receipt_credential_focus = 1;
    out->receipt_text_login = 0;
    out->receipt_text_login_fallback = 0;
    out->state = "receipt-credential-ready";
    out->message = "Credential receipt ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (record_plan->record_text_recovery && out->recovery_text_session_required) {
    out->receipt_ticket = "text-recovery-receipt-ticket";
    out->compositor_target = "text-recovery-receipt";
    out->receipt_text_recovery = 1;
    out->receipt_text_login = 1;
    out->receipt_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "receipt-text-recovery-ready";
    out->message = "Text recovery receipt ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (record_plan->record_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->receipt_ticket = "text-login-resume-receipt-ticket";
    out->compositor_target = "text-login-resume-receipt";
    out->receipt_policy = "full-receipt-declarative";
    out->receipt_text_login = 1;
    out->receipt_text_login_resume = 1;
    out->receipt_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "receipt-resume-ready";
    out->message = "Text login resume receipt ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->receipt_ticket = "text-login-fallback-receipt-ticket";
  out->compositor_target = "text-login-fallback-receipt";
  out->receipt_policy = "fallback-receipt-declarative";
  out->receipt_text_login = 1;
  out->receipt_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "receipt-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
