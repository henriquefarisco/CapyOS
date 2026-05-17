/*
 * src/auth/login_runtime/ledger_plan.c
 *
 * Credential-screen ledger plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.37 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the ledger stage of the credential pipeline:
 *
 *   - login_window_credential_screen_ledger_plan_reset (static)
 *   - login_window_credential_screen_ledger_plan_receipt_is_safe (static)
 *   - login_window_credential_screen_ledger_plan_build
 *
 * The ledger-plan converts a fail-closed receipt-plan into a ledger
 * contract for the downstream journal stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_receipt_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_ledger_plan_reset(
    struct login_window_credential_screen_ledger_plan *out,
    int receipt_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_LEDGER_PLAN_VERSION;
  out->receipt_plan_available = receipt_plan_available ? 1 : 0;
  out->receipt_plan_safe = 0;
  out->ledger_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->receipt_required = 1;
  out->receipt_allowed = 0;
  out->receipt_submitted = 0;
  out->receipt_ticket_selected = 0;
  out->receipt_target_selected = 0;
  out->receipt_persist_allowed = 0;
  out->receipt_persisted = 0;
  out->receipt_cpu_gpu_sync_allowed = 0;
  out->receipt_cpu_gpu_sync_submitted = 0;
  out->ledger_required = 1;
  out->ledger_allowed = 0;
  out->ledger_submitted = 0;
  out->ledger_ticket_selected = 0;
  out->ledger_target_selected = 0;
  out->ledger_persist_allowed = 0;
  out->ledger_persisted = 0;
  out->ledger_cpu_gpu_sync_allowed = 0;
  out->ledger_cpu_gpu_sync_submitted = 0;
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
  out->ledger_credential_panel = 0;
  out->ledger_credential_input = 0;
  out->ledger_credential_focus = 0;
  out->ledger_text_recovery = 0;
  out->ledger_text_login = 1;
  out->ledger_text_login_resume = 0;
  out->ledger_text_login_fallback = 1;
  out->ledger_error = 1;
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
  out->ledger_ticket = "text-login-fallback-ledger-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->record_policy = "record-disabled";
  out->receipt_policy = "receipt-disabled";
  out->ledger_policy = "ledger-disabled";
  out->event_type = "credential-screen-ledger-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "receipt-plan-unavailable";
}

static int login_window_credential_screen_ledger_plan_receipt_is_safe(
    const struct login_window_credential_screen_receipt_plan *receipt_plan) {
  return receipt_plan->receipt_plan_safe &&
         receipt_plan->receipt_required &&
         receipt_plan->receipt_allowed &&
         !receipt_plan->receipt_submitted &&
         receipt_plan->receipt_ticket_selected &&
         receipt_plan->receipt_target_selected &&
         !receipt_plan->receipt_persist_allowed &&
         !receipt_plan->receipt_persisted &&
         !receipt_plan->receipt_cpu_gpu_sync_allowed &&
         !receipt_plan->receipt_cpu_gpu_sync_submitted &&
         !receipt_plan->receipt_error &&
         receipt_plan->record_required &&
         receipt_plan->record_allowed &&
         !receipt_plan->record_submitted &&
         receipt_plan->record_ticket_selected &&
         receipt_plan->record_target_selected &&
         !receipt_plan->record_persist_allowed &&
         !receipt_plan->record_persisted &&
         !receipt_plan->record_cpu_gpu_sync_allowed &&
         !receipt_plan->record_cpu_gpu_sync_submitted &&
         !receipt_plan->record_error &&
         receipt_plan->audit_required &&
         receipt_plan->audit_allowed &&
         !receipt_plan->audit_submitted &&
         receipt_plan->audit_ticket_selected &&
         receipt_plan->audit_target_selected &&
         !receipt_plan->audit_log_append_allowed &&
         !receipt_plan->audit_log_appended &&
         !receipt_plan->audit_cpu_gpu_sync_allowed &&
         !receipt_plan->audit_cpu_gpu_sync_submitted &&
         receipt_plan->seal_required &&
         receipt_plan->seal_allowed &&
         !receipt_plan->seal_submitted &&
         receipt_plan->seal_ticket_selected &&
         receipt_plan->seal_target_selected &&
         !receipt_plan->seal_state_write_allowed &&
         !receipt_plan->seal_state_written &&
         !receipt_plan->seal_cpu_gpu_sync_allowed &&
         !receipt_plan->seal_cpu_gpu_sync_submitted &&
         receipt_plan->cleanup_required &&
         receipt_plan->cleanup_allowed &&
         !receipt_plan->cleanup_submitted &&
         receipt_plan->cleanup_ticket_selected &&
         receipt_plan->cleanup_target_selected &&
         !receipt_plan->cleanup_resource_release_allowed &&
         !receipt_plan->cleanup_resource_released &&
         !receipt_plan->cleanup_cpu_gpu_sync_allowed &&
         !receipt_plan->cleanup_cpu_gpu_sync_submitted &&
         receipt_plan->retire_required &&
         receipt_plan->retire_allowed &&
         !receipt_plan->retire_submitted &&
         receipt_plan->retire_ticket_selected &&
         receipt_plan->retire_target_selected &&
         !receipt_plan->retire_resource_release_allowed &&
         !receipt_plan->retire_resource_released &&
         !receipt_plan->retire_cpu_gpu_sync_allowed &&
         !receipt_plan->retire_cpu_gpu_sync_submitted &&
         receipt_plan->ack_required &&
         receipt_plan->ack_allowed &&
         !receipt_plan->ack_submitted &&
         receipt_plan->ack_ticket_selected &&
         receipt_plan->ack_target_selected &&
         !receipt_plan->ack_cpu_gpu_sync_allowed &&
         !receipt_plan->ack_cpu_gpu_sync_submitted &&
         receipt_plan->completion_required &&
         receipt_plan->completion_allowed &&
         receipt_plan->completion_report_required &&
         !receipt_plan->completion_reported &&
         receipt_plan->completion_ack_required &&
         !receipt_plan->completion_acknowledged &&
         receipt_plan->completion_ticket_selected &&
         receipt_plan->completion_target_selected &&
         !receipt_plan->completion_cpu_gpu_sync_allowed &&
         !receipt_plan->completion_cpu_gpu_sync_submitted &&
         receipt_plan->deadline_required &&
         receipt_plan->deadline_allowed &&
         !receipt_plan->deadline_armed &&
         receipt_plan->deadline_timer_required &&
         !receipt_plan->deadline_timer_armed &&
         !receipt_plan->deadline_expired &&
         receipt_plan->deadline_completion_required &&
         !receipt_plan->deadline_completion_reported &&
         !receipt_plan->deadline_cpu_gpu_sync_allowed &&
         !receipt_plan->deadline_cpu_gpu_sync_submitted &&
         !receipt_plan->sync_submitted &&
         !receipt_plan->sync_wait_allowed &&
         !receipt_plan->sync_wait_submitted &&
         !receipt_plan->sync_signal_allowed &&
         !receipt_plan->sync_signal_submitted &&
         !receipt_plan->sync_deadline_armed &&
         !receipt_plan->sync_completion_reported &&
         !receipt_plan->sync_cpu_gpu_sync_allowed &&
         !receipt_plan->sync_cpu_gpu_sync_submitted &&
         !receipt_plan->timeline_submitted &&
         !receipt_plan->timeline_wait_allowed &&
         !receipt_plan->timeline_wait_submitted &&
         !receipt_plan->timeline_signal_allowed &&
         !receipt_plan->timeline_signal_submitted &&
         !receipt_plan->timeline_semaphore_allowed &&
         !receipt_plan->timeline_semaphore_submitted &&
         !receipt_plan->timeline_value_allocated &&
         !receipt_plan->timeline_value_published &&
         !receipt_plan->timeline_cpu_gpu_sync_allowed &&
         !receipt_plan->timeline_cpu_gpu_sync_submitted &&
         !receipt_plan->fence_submitted &&
         !receipt_plan->fence_wait_allowed &&
         !receipt_plan->fence_wait_submitted &&
         !receipt_plan->fence_signal_allowed &&
         !receipt_plan->fence_signal_submitted &&
         !receipt_plan->fence_fd_export_allowed &&
         !receipt_plan->fence_fd_exported &&
         !receipt_plan->fence_cpu_gpu_sync_allowed &&
         !receipt_plan->fence_cpu_gpu_sync_submitted &&
         !receipt_plan->barrier_submitted &&
         !receipt_plan->barrier_memory_visibility_established &&
         !receipt_plan->barrier_cache_visibility_established &&
         !receipt_plan->barrier_cpu_gpu_sync_allowed &&
         !receipt_plan->barrier_cpu_gpu_sync_submitted &&
         !receipt_plan->flush_submitted &&
         !receipt_plan->flush_cache_clean_allowed &&
         !receipt_plan->flush_cache_cleaned &&
         !receipt_plan->flush_memory_barrier_allowed &&
         !receipt_plan->flush_memory_barrier_submitted &&
         !receipt_plan->framebuffer_submitted &&
         !receipt_plan->framebuffer_mapped &&
         !receipt_plan->framebuffer_write_allowed &&
         !receipt_plan->framebuffer_written &&
         !receipt_plan->framebuffer_flushed &&
         !receipt_plan->framebuffer_cache_cleaned &&
         !receipt_plan->blit_submitted &&
         !receipt_plan->blit_source_buffer_mapped &&
         !receipt_plan->blit_destination_buffer_mapped &&
         !receipt_plan->blit_pixels_copied &&
         !receipt_plan->blit_dma_allowed &&
         !receipt_plan->blit_dma_submitted &&
         !receipt_plan->output_submitted &&
         !receipt_plan->output_buffer_attached &&
         !receipt_plan->output_buffer_submitted &&
         !receipt_plan->output_flip_allowed &&
         !receipt_plan->output_flip_submitted &&
         !receipt_plan->display_submitted &&
         !receipt_plan->display_buffer_attached &&
         !receipt_plan->display_buffer_submitted &&
         !receipt_plan->display_mode_committed &&
         !receipt_plan->display_flip_allowed &&
         !receipt_plan->display_flip_submitted &&
         !receipt_plan->scanout_submitted &&
         !receipt_plan->scanout_buffer_attached &&
         !receipt_plan->scanout_buffer_submitted &&
         !receipt_plan->vsync_submitted &&
         !receipt_plan->vsync_wait_submitted &&
         !receipt_plan->vsync_fence_armed &&
         !receipt_plan->schedule_submitted &&
         !receipt_plan->present_submitted &&
         !receipt_plan->damage_submitted &&
         !receipt_plan->compositor_damage_submitted &&
         !receipt_plan->frame_timer_armed &&
         !receipt_plan->compositor_wake_allowed &&
         !receipt_plan->compositor_wake_submitted &&
         !receipt_plan->page_flip_allowed &&
         !receipt_plan->page_flip_submitted &&
         receipt_plan->route_selected &&
         !receipt_plan->route_blocked &&
         receipt_plan->credential_session_safe &&
         receipt_plan->credential_storage_wiped &&
         receipt_plan->credential_redacted &&
         receipt_plan->length_redacted &&
         !receipt_plan->raw_secret_exposed &&
         !receipt_plan->masked_text_exposed &&
         receipt_plan->submit_blocked &&
         !receipt_plan->submit_enabled &&
         !receipt_plan->auth_attempt_allowed &&
         !receipt_plan->submit_callback_bound &&
         !receipt_plan->auth_callback_bound &&
         receipt_plan->text_login_authoritative;
}

int login_window_credential_screen_ledger_plan_build(
    const struct login_window_credential_screen_receipt_plan *receipt_plan,
    struct login_window_credential_screen_ledger_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_ledger_plan_reset(out, receipt_plan ? 1 : 0);
  if (!receipt_plan) return 0;
  out->requested_action = receipt_plan->requested_action;
  out->receipt_plan_safe = receipt_plan->receipt_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_ledger_plan_receipt_is_safe(receipt_plan)) {
    out->event_type = "credential-screen-ledger-plan-unsafe";
    out->blocked_reason = "credential-ledger-plan-unsafe";
    out->message = "Credential screen ledger plan unsafe; use text login.";
    return 0;
  }
  out->ledger_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = receipt_plan->action_allowed ? 1 : 0;
  out->action_blocked = receipt_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = receipt_plan->input_focus_allowed ? 1 : 0;
  out->receipt_required = receipt_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = receipt_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = receipt_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = receipt_plan->receipt_target_selected ? 1 : 0;
  out->record_required = receipt_plan->record_required ? 1 : 0;
  out->record_allowed = receipt_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = receipt_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = receipt_plan->record_target_selected ? 1 : 0;
  out->audit_required = receipt_plan->audit_required ? 1 : 0;
  out->audit_allowed = receipt_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = receipt_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = receipt_plan->audit_target_selected ? 1 : 0;
  out->seal_required = receipt_plan->seal_required ? 1 : 0;
  out->seal_allowed = receipt_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = receipt_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = receipt_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = receipt_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = receipt_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = receipt_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = receipt_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = receipt_plan->retire_required ? 1 : 0;
  out->retire_allowed = receipt_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = receipt_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = receipt_plan->retire_target_selected ? 1 : 0;
  out->ack_required = receipt_plan->ack_required ? 1 : 0;
  out->ack_allowed = receipt_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = receipt_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = receipt_plan->ack_target_selected ? 1 : 0;
  out->completion_required = receipt_plan->completion_required ? 1 : 0;
  out->completion_allowed = receipt_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = receipt_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = receipt_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = receipt_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = receipt_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = receipt_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = receipt_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = receipt_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = receipt_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = receipt_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = receipt_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = receipt_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = receipt_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = receipt_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = receipt_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = receipt_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = receipt_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = receipt_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = receipt_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume = receipt_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback = receipt_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_allowed = 1;
  out->ledger_ticket_selected = 1;
  out->ledger_target_selected = 1;
  out->ledger_error = 0;
  out->recovery_text_session_required = receipt_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = receipt_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = receipt_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = receipt_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = receipt_plan->view;
  out->widget_tree = receipt_plan->widget_tree;
  out->record_ticket = receipt_plan->record_ticket;
  out->receipt_ticket = receipt_plan->receipt_ticket;
  out->focus_target = receipt_plan->focus_target;
  out->primary_action = receipt_plan->primary_action;
  out->route = receipt_plan->route;
  out->compositor_target = receipt_plan->compositor_target;
  out->record_policy = receipt_plan->record_policy;
  out->receipt_policy = receipt_plan->receipt_policy;
  out->ledger_policy = "declarative-ledger-no-persist";
  out->event_type = "credential-screen-ledger-plan-ready";
  out->state = "ledger-ready";
  out->message = "Credential screen ledger ticket ready; no ledger persisted.";
  out->blocked_reason = receipt_plan->blocked_reason;
  if (receipt_plan->submit_requested) {
    out->ledger_ticket = "text-login-fallback-ledger-ticket";
    out->compositor_target = "text-login-fallback-ledger";
    out->ledger_policy = "fallback-ledger-declarative";
    out->ledger_text_login = 1;
    out->ledger_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "ledger-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (receipt_plan->receipt_credential_panel && receipt_plan->receipt_credential_input &&
      receipt_plan->receipt_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->ledger_ticket = "credential-screen-ledger-ticket";
    out->compositor_target = "credential-screen-ledger";
    out->ledger_credential_panel = 1;
    out->ledger_credential_input = 1;
    out->ledger_credential_focus = 1;
    out->ledger_text_login = 0;
    out->ledger_text_login_fallback = 0;
    out->state = "ledger-credential-ready";
    out->message = "Credential ledger ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (receipt_plan->receipt_text_recovery && out->recovery_text_session_required) {
    out->ledger_ticket = "text-recovery-ledger-ticket";
    out->compositor_target = "text-recovery-ledger";
    out->ledger_text_recovery = 1;
    out->ledger_text_login = 1;
    out->ledger_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "ledger-text-recovery-ready";
    out->message = "Text recovery ledger ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (receipt_plan->receipt_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->ledger_ticket = "text-login-resume-ledger-ticket";
    out->compositor_target = "text-login-resume-ledger";
    out->ledger_policy = "full-ledger-declarative";
    out->ledger_text_login = 1;
    out->ledger_text_login_resume = 1;
    out->ledger_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "ledger-resume-ready";
    out->message = "Text login resume ledger ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->ledger_ticket = "text-login-fallback-ledger-ticket";
  out->compositor_target = "text-login-fallback-ledger";
  out->ledger_policy = "fallback-ledger-declarative";
  out->ledger_text_login = 1;
  out->ledger_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "ledger-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
