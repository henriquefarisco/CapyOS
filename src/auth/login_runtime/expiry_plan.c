/*
 * src/auth/login_runtime/expiry_plan.c
 *
 * Credential-screen expiry plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.41 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the expiry stage of the credential pipeline:
 *
 *   - login_window_credential_screen_expiry_plan_reset (static)
 *   - login_window_credential_screen_expiry_plan_retention_is_safe (static)
 *   - login_window_credential_screen_expiry_plan_build
 *
 * The expiry-plan converts a fail-closed retention-plan into an
 * expiry contract for the downstream purge stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_retention_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.  This file
 * closes Phase 4 of the Estagio C dedicated plan.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_expiry_plan_reset(
    struct login_window_credential_screen_expiry_plan *out,
    int retention_plan_available) {
  *out = (struct login_window_credential_screen_expiry_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_EXPIRY_PLAN_VERSION;
  out->retention_plan_available = retention_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
  out->archive_required = 1;
  out->retention_required = 1;
  out->expiry_required = 1;
  out->record_required = 1;
  out->audit_required = 1;
  out->seal_required = 1;
  out->cleanup_required = 1;
  out->retire_required = 1;
  out->ack_required = 1;
  out->completion_required = 1;
  out->completion_report_required = 1;
  out->completion_ack_required = 1;
  out->deadline_required = 1;
  out->deadline_timer_required = 1;
  out->deadline_completion_required = 1;
  out->record_text_login = 1;
  out->record_text_login_fallback = 1;
  out->record_error = 1;
  out->receipt_text_login = 1;
  out->receipt_text_login_fallback = 1;
  out->receipt_error = 1;
  out->ledger_text_login = 1;
  out->ledger_text_login_fallback = 1;
  out->ledger_error = 1;
  out->journal_text_login = 1;
  out->journal_text_login_fallback = 1;
  out->journal_error = 1;
  out->archive_text_login = 1;
  out->archive_text_login_fallback = 1;
  out->archive_error = 1;
  out->retention_text_login = 1;
  out->retention_text_login_fallback = 1;
  out->retention_error = 1;
  out->expiry_text_login = 1;
  out->expiry_text_login_fallback = 1;
  out->expiry_error = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->record_ticket = "text-login-fallback-record-ticket";
  out->receipt_ticket = "text-login-fallback-receipt-ticket";
  out->ledger_ticket = "text-login-fallback-ledger-ticket";
  out->journal_ticket = "text-login-fallback-journal-ticket";
  out->archive_ticket = "text-login-fallback-archive-ticket";
  out->retention_ticket = "text-login-fallback-retention-ticket";
  out->expiry_ticket = "text-login-fallback-expiry-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->record_policy = "record-disabled";
  out->receipt_policy = "receipt-disabled";
  out->ledger_policy = "ledger-disabled";
  out->journal_policy = "journal-disabled";
  out->archive_policy = "archive-disabled";
  out->retention_policy = "retention-disabled";
  out->expiry_policy = "expiry-disabled";
  out->event_type = "credential-screen-expiry-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "retention-plan-unavailable";
}

static int login_window_credential_screen_expiry_plan_retention_is_safe(
    const struct login_window_credential_screen_retention_plan *retention_plan) {
  return retention_plan->retention_plan_safe &&
         retention_plan->retention_required &&
         retention_plan->retention_allowed &&
         !retention_plan->retention_submitted &&
         retention_plan->retention_ticket_selected &&
         retention_plan->retention_target_selected &&
         !retention_plan->retention_persist_allowed &&
         !retention_plan->retention_persisted &&
         !retention_plan->retention_cpu_gpu_sync_allowed &&
         !retention_plan->retention_cpu_gpu_sync_submitted &&
         !retention_plan->retention_error &&
         retention_plan->archive_required &&
         retention_plan->archive_allowed &&
         !retention_plan->archive_submitted &&
         retention_plan->archive_ticket_selected &&
         retention_plan->archive_target_selected &&
         !retention_plan->archive_persist_allowed &&
         !retention_plan->archive_persisted &&
         !retention_plan->archive_cpu_gpu_sync_allowed &&
         !retention_plan->archive_cpu_gpu_sync_submitted &&
         !retention_plan->archive_error &&
         retention_plan->journal_required &&
         retention_plan->journal_allowed &&
         !retention_plan->journal_submitted &&
         retention_plan->journal_ticket_selected &&
         retention_plan->journal_target_selected &&
         !retention_plan->journal_persist_allowed &&
         !retention_plan->journal_persisted &&
         !retention_plan->journal_cpu_gpu_sync_allowed &&
         !retention_plan->journal_cpu_gpu_sync_submitted &&
         !retention_plan->journal_error &&
         retention_plan->ledger_required &&
         retention_plan->ledger_allowed &&
         !retention_plan->ledger_submitted &&
         retention_plan->ledger_ticket_selected &&
         retention_plan->ledger_target_selected &&
         !retention_plan->ledger_persist_allowed &&
         !retention_plan->ledger_persisted &&
         !retention_plan->ledger_cpu_gpu_sync_allowed &&
         !retention_plan->ledger_cpu_gpu_sync_submitted &&
         !retention_plan->ledger_error &&
         retention_plan->receipt_required &&
         retention_plan->receipt_allowed &&
         !retention_plan->receipt_submitted &&
         retention_plan->receipt_ticket_selected &&
         retention_plan->receipt_target_selected &&
         !retention_plan->receipt_persist_allowed &&
         !retention_plan->receipt_persisted &&
         !retention_plan->receipt_cpu_gpu_sync_allowed &&
         !retention_plan->receipt_cpu_gpu_sync_submitted &&
         !retention_plan->receipt_error &&
         retention_plan->record_required &&
         retention_plan->record_allowed &&
         !retention_plan->record_submitted &&
         retention_plan->record_ticket_selected &&
         retention_plan->record_target_selected &&
         !retention_plan->record_persist_allowed &&
         !retention_plan->record_persisted &&
         !retention_plan->record_cpu_gpu_sync_allowed &&
         !retention_plan->record_cpu_gpu_sync_submitted &&
         !retention_plan->record_error &&
         retention_plan->audit_required &&
         retention_plan->audit_allowed &&
         !retention_plan->audit_submitted &&
         retention_plan->audit_ticket_selected &&
         retention_plan->audit_target_selected &&
         !retention_plan->audit_log_append_allowed &&
         !retention_plan->audit_log_appended &&
         !retention_plan->audit_cpu_gpu_sync_allowed &&
         !retention_plan->audit_cpu_gpu_sync_submitted &&
         retention_plan->seal_required &&
         retention_plan->seal_allowed &&
         !retention_plan->seal_submitted &&
         retention_plan->seal_ticket_selected &&
         retention_plan->seal_target_selected &&
         !retention_plan->seal_state_write_allowed &&
         !retention_plan->seal_state_written &&
         !retention_plan->seal_cpu_gpu_sync_allowed &&
         !retention_plan->seal_cpu_gpu_sync_submitted &&
         retention_plan->cleanup_required &&
         retention_plan->cleanup_allowed &&
         !retention_plan->cleanup_submitted &&
         retention_plan->cleanup_ticket_selected &&
         retention_plan->cleanup_target_selected &&
         !retention_plan->cleanup_resource_release_allowed &&
         !retention_plan->cleanup_resource_released &&
         !retention_plan->cleanup_cpu_gpu_sync_allowed &&
         !retention_plan->cleanup_cpu_gpu_sync_submitted &&
         retention_plan->retire_required &&
         retention_plan->retire_allowed &&
         !retention_plan->retire_submitted &&
         retention_plan->retire_ticket_selected &&
         retention_plan->retire_target_selected &&
         !retention_plan->retire_resource_release_allowed &&
         !retention_plan->retire_resource_released &&
         !retention_plan->retire_cpu_gpu_sync_allowed &&
         !retention_plan->retire_cpu_gpu_sync_submitted &&
         retention_plan->ack_required &&
         retention_plan->ack_allowed &&
         !retention_plan->ack_submitted &&
         retention_plan->ack_ticket_selected &&
         retention_plan->ack_target_selected &&
         !retention_plan->ack_cpu_gpu_sync_allowed &&
         !retention_plan->ack_cpu_gpu_sync_submitted &&
         retention_plan->completion_required &&
         retention_plan->completion_allowed &&
         retention_plan->completion_report_required &&
         !retention_plan->completion_reported &&
         retention_plan->completion_ack_required &&
         !retention_plan->completion_acknowledged &&
         retention_plan->completion_ticket_selected &&
         retention_plan->completion_target_selected &&
         !retention_plan->completion_cpu_gpu_sync_allowed &&
         !retention_plan->completion_cpu_gpu_sync_submitted &&
         retention_plan->deadline_required &&
         retention_plan->deadline_allowed &&
         !retention_plan->deadline_armed &&
         retention_plan->deadline_timer_required &&
         !retention_plan->deadline_timer_armed &&
         !retention_plan->deadline_expired &&
         retention_plan->deadline_completion_required &&
         !retention_plan->deadline_completion_reported &&
         !retention_plan->deadline_cpu_gpu_sync_allowed &&
         !retention_plan->deadline_cpu_gpu_sync_submitted &&
         !retention_plan->sync_submitted &&
         !retention_plan->sync_wait_allowed &&
         !retention_plan->sync_wait_submitted &&
         !retention_plan->sync_signal_allowed &&
         !retention_plan->sync_signal_submitted &&
         !retention_plan->sync_deadline_armed &&
         !retention_plan->sync_completion_reported &&
         !retention_plan->sync_cpu_gpu_sync_allowed &&
         !retention_plan->sync_cpu_gpu_sync_submitted &&
         !retention_plan->timeline_submitted &&
         !retention_plan->timeline_wait_allowed &&
         !retention_plan->timeline_wait_submitted &&
         !retention_plan->timeline_signal_allowed &&
         !retention_plan->timeline_signal_submitted &&
         !retention_plan->timeline_semaphore_allowed &&
         !retention_plan->timeline_semaphore_submitted &&
         !retention_plan->timeline_value_allocated &&
         !retention_plan->timeline_value_published &&
         !retention_plan->timeline_cpu_gpu_sync_allowed &&
         !retention_plan->timeline_cpu_gpu_sync_submitted &&
         !retention_plan->fence_submitted &&
         !retention_plan->fence_wait_allowed &&
         !retention_plan->fence_wait_submitted &&
         !retention_plan->fence_signal_allowed &&
         !retention_plan->fence_signal_submitted &&
         !retention_plan->fence_fd_export_allowed &&
         !retention_plan->fence_fd_exported &&
         !retention_plan->fence_cpu_gpu_sync_allowed &&
         !retention_plan->fence_cpu_gpu_sync_submitted &&
         !retention_plan->barrier_submitted &&
         !retention_plan->barrier_memory_visibility_established &&
         !retention_plan->barrier_cache_visibility_established &&
         !retention_plan->barrier_cpu_gpu_sync_allowed &&
         !retention_plan->barrier_cpu_gpu_sync_submitted &&
         !retention_plan->flush_submitted &&
         !retention_plan->flush_cache_clean_allowed &&
         !retention_plan->flush_cache_cleaned &&
         !retention_plan->flush_memory_barrier_allowed &&
         !retention_plan->flush_memory_barrier_submitted &&
         !retention_plan->framebuffer_submitted &&
         !retention_plan->framebuffer_mapped &&
         !retention_plan->framebuffer_write_allowed &&
         !retention_plan->framebuffer_written &&
         !retention_plan->framebuffer_flushed &&
         !retention_plan->framebuffer_cache_cleaned &&
         !retention_plan->blit_submitted &&
         !retention_plan->blit_source_buffer_mapped &&
         !retention_plan->blit_destination_buffer_mapped &&
         !retention_plan->blit_pixels_copied &&
         !retention_plan->blit_dma_allowed &&
         !retention_plan->blit_dma_submitted &&
         !retention_plan->output_submitted &&
         !retention_plan->output_buffer_attached &&
         !retention_plan->output_buffer_submitted &&
         !retention_plan->output_flip_allowed &&
         !retention_plan->output_flip_submitted &&
         !retention_plan->display_submitted &&
         !retention_plan->display_buffer_attached &&
         !retention_plan->display_buffer_submitted &&
         !retention_plan->display_mode_committed &&
         !retention_plan->display_flip_allowed &&
         !retention_plan->display_flip_submitted &&
         !retention_plan->scanout_submitted &&
         !retention_plan->scanout_buffer_attached &&
         !retention_plan->scanout_buffer_submitted &&
         !retention_plan->vsync_submitted &&
         !retention_plan->vsync_wait_submitted &&
         !retention_plan->vsync_fence_armed &&
         !retention_plan->schedule_submitted &&
         !retention_plan->present_submitted &&
         !retention_plan->damage_submitted &&
         !retention_plan->compositor_damage_submitted &&
         !retention_plan->frame_timer_armed &&
         !retention_plan->compositor_wake_allowed &&
         !retention_plan->compositor_wake_submitted &&
         !retention_plan->page_flip_allowed &&
         !retention_plan->page_flip_submitted &&
         retention_plan->route_selected &&
         !retention_plan->route_blocked &&
         retention_plan->credential_session_safe &&
         retention_plan->credential_storage_wiped &&
         retention_plan->credential_redacted &&
         retention_plan->length_redacted &&
         !retention_plan->raw_secret_exposed &&
         !retention_plan->masked_text_exposed &&
         retention_plan->submit_blocked &&
         !retention_plan->submit_enabled &&
         !retention_plan->auth_attempt_allowed &&
         !retention_plan->submit_callback_bound &&
         !retention_plan->auth_callback_bound &&
         retention_plan->text_login_authoritative;
}

int login_window_credential_screen_expiry_plan_build(
    const struct login_window_credential_screen_retention_plan *retention_plan,
    struct login_window_credential_screen_expiry_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_expiry_plan_reset(out, retention_plan ? 1 : 0);
  if (!retention_plan) return 0;
  out->requested_action = retention_plan->requested_action;
  out->retention_plan_safe = retention_plan->retention_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_expiry_plan_retention_is_safe(retention_plan)) {
    out->event_type = "credential-screen-expiry-plan-unsafe";
    out->blocked_reason = "credential-expiry-plan-unsafe";
    out->message = "Credential screen expiry plan unsafe; use text login.";
    return 0;
  }
  out->expiry_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = retention_plan->action_allowed ? 1 : 0;
  out->action_blocked = retention_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = retention_plan->input_focus_allowed ? 1 : 0;
  out->receipt_required = retention_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = retention_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = retention_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = retention_plan->receipt_target_selected ? 1 : 0;
  out->ledger_required = retention_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = retention_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = retention_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = retention_plan->ledger_target_selected ? 1 : 0;
  out->journal_required = retention_plan->journal_required ? 1 : 0;
  out->journal_allowed = retention_plan->journal_allowed ? 1 : 0;
  out->journal_ticket_selected = retention_plan->journal_ticket_selected ? 1 : 0;
  out->journal_target_selected = retention_plan->journal_target_selected ? 1 : 0;
  out->archive_required = retention_plan->archive_required ? 1 : 0;
  out->archive_allowed = retention_plan->archive_allowed ? 1 : 0;
  out->archive_ticket_selected = retention_plan->archive_ticket_selected ? 1 : 0;
  out->archive_target_selected = retention_plan->archive_target_selected ? 1 : 0;
  out->retention_required = retention_plan->retention_required ? 1 : 0;
  out->retention_allowed = retention_plan->retention_allowed ? 1 : 0;
  out->retention_ticket_selected = retention_plan->retention_ticket_selected ? 1 : 0;
  out->retention_target_selected = retention_plan->retention_target_selected ? 1 : 0;
  out->record_required = retention_plan->record_required ? 1 : 0;
  out->record_allowed = retention_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = retention_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = retention_plan->record_target_selected ? 1 : 0;
  out->audit_required = retention_plan->audit_required ? 1 : 0;
  out->audit_allowed = retention_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = retention_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = retention_plan->audit_target_selected ? 1 : 0;
  out->seal_required = retention_plan->seal_required ? 1 : 0;
  out->seal_allowed = retention_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = retention_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = retention_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = retention_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = retention_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = retention_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = retention_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = retention_plan->retire_required ? 1 : 0;
  out->retire_allowed = retention_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = retention_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = retention_plan->retire_target_selected ? 1 : 0;
  out->ack_required = retention_plan->ack_required ? 1 : 0;
  out->ack_allowed = retention_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = retention_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = retention_plan->ack_target_selected ? 1 : 0;
  out->completion_required = retention_plan->completion_required ? 1 : 0;
  out->completion_allowed = retention_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = retention_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = retention_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = retention_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = retention_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = retention_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = retention_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = retention_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = retention_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = retention_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = retention_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = retention_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = retention_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = retention_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = retention_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = retention_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = retention_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = retention_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = retention_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume = retention_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback = retention_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_credential_panel = retention_plan->ledger_credential_panel ? 1 : 0;
  out->ledger_credential_input = retention_plan->ledger_credential_input ? 1 : 0;
  out->ledger_credential_focus = retention_plan->ledger_credential_focus ? 1 : 0;
  out->ledger_text_recovery = retention_plan->ledger_text_recovery ? 1 : 0;
  out->ledger_text_login = retention_plan->ledger_text_login ? 1 : 0;
  out->ledger_text_login_resume = retention_plan->ledger_text_login_resume ? 1 : 0;
  out->ledger_text_login_fallback = retention_plan->ledger_text_login_fallback ? 1 : 0;
  out->ledger_error = 0;
  out->journal_credential_panel = retention_plan->journal_credential_panel ? 1 : 0;
  out->journal_credential_input = retention_plan->journal_credential_input ? 1 : 0;
  out->journal_credential_focus = retention_plan->journal_credential_focus ? 1 : 0;
  out->journal_text_recovery = retention_plan->journal_text_recovery ? 1 : 0;
  out->journal_text_login = retention_plan->journal_text_login ? 1 : 0;
  out->journal_text_login_resume = retention_plan->journal_text_login_resume ? 1 : 0;
  out->journal_text_login_fallback = retention_plan->journal_text_login_fallback ? 1 : 0;
  out->journal_error = 0;
  out->archive_credential_panel = retention_plan->archive_credential_panel ? 1 : 0;
  out->archive_credential_input = retention_plan->archive_credential_input ? 1 : 0;
  out->archive_credential_focus = retention_plan->archive_credential_focus ? 1 : 0;
  out->archive_text_recovery = retention_plan->archive_text_recovery ? 1 : 0;
  out->archive_text_login = retention_plan->archive_text_login ? 1 : 0;
  out->archive_text_login_resume = retention_plan->archive_text_login_resume ? 1 : 0;
  out->archive_text_login_fallback = retention_plan->archive_text_login_fallback ? 1 : 0;
  out->archive_error = 0;
  out->retention_credential_panel = retention_plan->retention_credential_panel ? 1 : 0;
  out->retention_credential_input = retention_plan->retention_credential_input ? 1 : 0;
  out->retention_credential_focus = retention_plan->retention_credential_focus ? 1 : 0;
  out->retention_text_recovery = retention_plan->retention_text_recovery ? 1 : 0;
  out->retention_text_login = retention_plan->retention_text_login ? 1 : 0;
  out->retention_text_login_resume = retention_plan->retention_text_login_resume ? 1 : 0;
  out->retention_text_login_fallback = retention_plan->retention_text_login_fallback ? 1 : 0;
  out->retention_error = 0;
  out->expiry_allowed = 1;
  out->expiry_ticket_selected = 1;
  out->expiry_target_selected = 1;
  out->expiry_error = 0;
  out->recovery_text_session_required = retention_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = retention_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = retention_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = retention_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = retention_plan->view;
  out->widget_tree = retention_plan->widget_tree;
  out->record_ticket = retention_plan->record_ticket;
  out->receipt_ticket = retention_plan->receipt_ticket;
  out->ledger_ticket = retention_plan->ledger_ticket;
  out->journal_ticket = retention_plan->journal_ticket;
  out->archive_ticket = retention_plan->archive_ticket;
  out->retention_ticket = retention_plan->retention_ticket;
  out->focus_target = retention_plan->focus_target;
  out->primary_action = retention_plan->primary_action;
  out->route = retention_plan->route;
  out->compositor_target = retention_plan->compositor_target;
  out->record_policy = retention_plan->record_policy;
  out->receipt_policy = retention_plan->receipt_policy;
  out->ledger_policy = retention_plan->ledger_policy;
  out->journal_policy = retention_plan->journal_policy;
  out->archive_policy = retention_plan->archive_policy;
  out->retention_policy = retention_plan->retention_policy;
  out->expiry_policy = "declarative-expiry-no-persist";
  out->event_type = "credential-screen-expiry-plan-ready";
  out->state = "expiry-ready";
  out->message = "Credential screen expiry ticket ready; no expiry persisted.";
  out->blocked_reason = retention_plan->blocked_reason;
  if (retention_plan->submit_requested) {
    out->expiry_ticket = "text-login-fallback-expiry-ticket";
    out->compositor_target = "text-login-fallback-expiry";
    out->expiry_policy = "fallback-expiry-declarative";
    out->expiry_text_login = 1;
    out->expiry_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "expiry-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (retention_plan->retention_credential_panel &&
      retention_plan->retention_credential_input &&
      retention_plan->retention_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->expiry_ticket = "credential-screen-expiry-ticket";
    out->compositor_target = "credential-screen-expiry";
    out->expiry_credential_panel = 1;
    out->expiry_credential_input = 1;
    out->expiry_credential_focus = 1;
    out->expiry_text_login = 0;
    out->expiry_text_login_fallback = 0;
    out->state = "expiry-credential-ready";
    out->message = "Credential expiry ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (retention_plan->retention_text_recovery && out->recovery_text_session_required) {
    out->expiry_ticket = "text-recovery-expiry-ticket";
    out->compositor_target = "text-recovery-expiry";
    out->expiry_text_recovery = 1;
    out->expiry_text_login = 1;
    out->expiry_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "expiry-text-recovery-ready";
    out->message = "Text recovery expiry ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (retention_plan->retention_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->expiry_ticket = "text-login-resume-expiry-ticket";
    out->compositor_target = "text-login-resume-expiry";
    out->expiry_policy = "full-expiry-declarative";
    out->expiry_text_login = 1;
    out->expiry_text_login_resume = 1;
    out->expiry_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "expiry-resume-ready";
    out->message = "Text login resume expiry ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->expiry_ticket = "text-login-fallback-expiry-ticket";
  out->compositor_target = "text-login-fallback-expiry";
  out->expiry_policy = "fallback-expiry-declarative";
  out->expiry_text_login = 1;
  out->expiry_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "expiry-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
