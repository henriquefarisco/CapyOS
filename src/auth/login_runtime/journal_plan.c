/*
 * src/auth/login_runtime/journal_plan.c
 *
 * Credential-screen journal plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.38 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the journal stage of the credential pipeline:
 *
 *   - login_window_credential_screen_journal_plan_reset (static)
 *   - login_window_credential_screen_journal_plan_ledger_is_safe (static)
 *   - login_window_credential_screen_journal_plan_build
 *
 * The journal-plan converts a fail-closed ledger-plan into a
 * journal contract for the downstream archive stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_ledger_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_journal_plan_reset(
    struct login_window_credential_screen_journal_plan *out,
    int ledger_plan_available) {
  *out = (struct login_window_credential_screen_journal_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_JOURNAL_PLAN_VERSION;
  out->ledger_plan_available = ledger_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->record_policy = "record-disabled";
  out->receipt_policy = "receipt-disabled";
  out->ledger_policy = "ledger-disabled";
  out->journal_policy = "journal-disabled";
  out->event_type = "credential-screen-journal-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "ledger-plan-unavailable";
}

static int login_window_credential_screen_journal_plan_ledger_is_safe(
    const struct login_window_credential_screen_ledger_plan *ledger_plan) {
  return ledger_plan->ledger_plan_safe &&
         ledger_plan->ledger_required &&
         ledger_plan->ledger_allowed &&
         !ledger_plan->ledger_submitted &&
         ledger_plan->ledger_ticket_selected &&
         ledger_plan->ledger_target_selected &&
         !ledger_plan->ledger_persist_allowed &&
         !ledger_plan->ledger_persisted &&
         !ledger_plan->ledger_cpu_gpu_sync_allowed &&
         !ledger_plan->ledger_cpu_gpu_sync_submitted &&
         !ledger_plan->ledger_error &&
         ledger_plan->receipt_required &&
         ledger_plan->receipt_allowed &&
         !ledger_plan->receipt_submitted &&
         ledger_plan->receipt_ticket_selected &&
         ledger_plan->receipt_target_selected &&
         !ledger_plan->receipt_persist_allowed &&
         !ledger_plan->receipt_persisted &&
         !ledger_plan->receipt_cpu_gpu_sync_allowed &&
         !ledger_plan->receipt_cpu_gpu_sync_submitted &&
         !ledger_plan->receipt_error &&
         ledger_plan->record_required &&
         ledger_plan->record_allowed &&
         !ledger_plan->record_submitted &&
         ledger_plan->record_ticket_selected &&
         ledger_plan->record_target_selected &&
         !ledger_plan->record_persist_allowed &&
         !ledger_plan->record_persisted &&
         !ledger_plan->record_cpu_gpu_sync_allowed &&
         !ledger_plan->record_cpu_gpu_sync_submitted &&
         !ledger_plan->record_error &&
         ledger_plan->audit_required &&
         ledger_plan->audit_allowed &&
         !ledger_plan->audit_submitted &&
         ledger_plan->audit_ticket_selected &&
         ledger_plan->audit_target_selected &&
         !ledger_plan->audit_log_append_allowed &&
         !ledger_plan->audit_log_appended &&
         !ledger_plan->audit_cpu_gpu_sync_allowed &&
         !ledger_plan->audit_cpu_gpu_sync_submitted &&
         ledger_plan->seal_required &&
         ledger_plan->seal_allowed &&
         !ledger_plan->seal_submitted &&
         ledger_plan->seal_ticket_selected &&
         ledger_plan->seal_target_selected &&
         !ledger_plan->seal_state_write_allowed &&
         !ledger_plan->seal_state_written &&
         !ledger_plan->seal_cpu_gpu_sync_allowed &&
         !ledger_plan->seal_cpu_gpu_sync_submitted &&
         ledger_plan->cleanup_required &&
         ledger_plan->cleanup_allowed &&
         !ledger_plan->cleanup_submitted &&
         ledger_plan->cleanup_ticket_selected &&
         ledger_plan->cleanup_target_selected &&
         !ledger_plan->cleanup_resource_release_allowed &&
         !ledger_plan->cleanup_resource_released &&
         !ledger_plan->cleanup_cpu_gpu_sync_allowed &&
         !ledger_plan->cleanup_cpu_gpu_sync_submitted &&
         ledger_plan->retire_required &&
         ledger_plan->retire_allowed &&
         !ledger_plan->retire_submitted &&
         ledger_plan->retire_ticket_selected &&
         ledger_plan->retire_target_selected &&
         !ledger_plan->retire_resource_release_allowed &&
         !ledger_plan->retire_resource_released &&
         !ledger_plan->retire_cpu_gpu_sync_allowed &&
         !ledger_plan->retire_cpu_gpu_sync_submitted &&
         ledger_plan->ack_required &&
         ledger_plan->ack_allowed &&
         !ledger_plan->ack_submitted &&
         ledger_plan->ack_ticket_selected &&
         ledger_plan->ack_target_selected &&
         !ledger_plan->ack_cpu_gpu_sync_allowed &&
         !ledger_plan->ack_cpu_gpu_sync_submitted &&
         ledger_plan->completion_required &&
         ledger_plan->completion_allowed &&
         ledger_plan->completion_report_required &&
         !ledger_plan->completion_reported &&
         ledger_plan->completion_ack_required &&
         !ledger_plan->completion_acknowledged &&
         ledger_plan->completion_ticket_selected &&
         ledger_plan->completion_target_selected &&
         !ledger_plan->completion_cpu_gpu_sync_allowed &&
         !ledger_plan->completion_cpu_gpu_sync_submitted &&
         ledger_plan->deadline_required &&
         ledger_plan->deadline_allowed &&
         !ledger_plan->deadline_armed &&
         ledger_plan->deadline_timer_required &&
         !ledger_plan->deadline_timer_armed &&
         !ledger_plan->deadline_expired &&
         ledger_plan->deadline_completion_required &&
         !ledger_plan->deadline_completion_reported &&
         !ledger_plan->deadline_cpu_gpu_sync_allowed &&
         !ledger_plan->deadline_cpu_gpu_sync_submitted &&
         !ledger_plan->sync_submitted &&
         !ledger_plan->sync_wait_allowed &&
         !ledger_plan->sync_wait_submitted &&
         !ledger_plan->sync_signal_allowed &&
         !ledger_plan->sync_signal_submitted &&
         !ledger_plan->sync_deadline_armed &&
         !ledger_plan->sync_completion_reported &&
         !ledger_plan->sync_cpu_gpu_sync_allowed &&
         !ledger_plan->sync_cpu_gpu_sync_submitted &&
         !ledger_plan->timeline_submitted &&
         !ledger_plan->timeline_wait_allowed &&
         !ledger_plan->timeline_wait_submitted &&
         !ledger_plan->timeline_signal_allowed &&
         !ledger_plan->timeline_signal_submitted &&
         !ledger_plan->timeline_semaphore_allowed &&
         !ledger_plan->timeline_semaphore_submitted &&
         !ledger_plan->timeline_value_allocated &&
         !ledger_plan->timeline_value_published &&
         !ledger_plan->timeline_cpu_gpu_sync_allowed &&
         !ledger_plan->timeline_cpu_gpu_sync_submitted &&
         !ledger_plan->fence_submitted &&
         !ledger_plan->fence_wait_allowed &&
         !ledger_plan->fence_wait_submitted &&
         !ledger_plan->fence_signal_allowed &&
         !ledger_plan->fence_signal_submitted &&
         !ledger_plan->fence_fd_export_allowed &&
         !ledger_plan->fence_fd_exported &&
         !ledger_plan->fence_cpu_gpu_sync_allowed &&
         !ledger_plan->fence_cpu_gpu_sync_submitted &&
         !ledger_plan->barrier_submitted &&
         !ledger_plan->barrier_memory_visibility_established &&
         !ledger_plan->barrier_cache_visibility_established &&
         !ledger_plan->barrier_cpu_gpu_sync_allowed &&
         !ledger_plan->barrier_cpu_gpu_sync_submitted &&
         !ledger_plan->flush_submitted &&
         !ledger_plan->flush_cache_clean_allowed &&
         !ledger_plan->flush_cache_cleaned &&
         !ledger_plan->flush_memory_barrier_allowed &&
         !ledger_plan->flush_memory_barrier_submitted &&
         !ledger_plan->framebuffer_submitted &&
         !ledger_plan->framebuffer_mapped &&
         !ledger_plan->framebuffer_write_allowed &&
         !ledger_plan->framebuffer_written &&
         !ledger_plan->framebuffer_flushed &&
         !ledger_plan->framebuffer_cache_cleaned &&
         !ledger_plan->blit_submitted &&
         !ledger_plan->blit_source_buffer_mapped &&
         !ledger_plan->blit_destination_buffer_mapped &&
         !ledger_plan->blit_pixels_copied &&
         !ledger_plan->blit_dma_allowed &&
         !ledger_plan->blit_dma_submitted &&
         !ledger_plan->output_submitted &&
         !ledger_plan->output_buffer_attached &&
         !ledger_plan->output_buffer_submitted &&
         !ledger_plan->output_flip_allowed &&
         !ledger_plan->output_flip_submitted &&
         !ledger_plan->display_submitted &&
         !ledger_plan->display_buffer_attached &&
         !ledger_plan->display_buffer_submitted &&
         !ledger_plan->display_mode_committed &&
         !ledger_plan->display_flip_allowed &&
         !ledger_plan->display_flip_submitted &&
         !ledger_plan->scanout_submitted &&
         !ledger_plan->scanout_buffer_attached &&
         !ledger_plan->scanout_buffer_submitted &&
         !ledger_plan->vsync_submitted &&
         !ledger_plan->vsync_wait_submitted &&
         !ledger_plan->vsync_fence_armed &&
         !ledger_plan->schedule_submitted &&
         !ledger_plan->present_submitted &&
         !ledger_plan->damage_submitted &&
         !ledger_plan->compositor_damage_submitted &&
         !ledger_plan->frame_timer_armed &&
         !ledger_plan->compositor_wake_allowed &&
         !ledger_plan->compositor_wake_submitted &&
         !ledger_plan->page_flip_allowed &&
         !ledger_plan->page_flip_submitted &&
         ledger_plan->route_selected &&
         !ledger_plan->route_blocked &&
         ledger_plan->credential_session_safe &&
         ledger_plan->credential_storage_wiped &&
         ledger_plan->credential_redacted &&
         ledger_plan->length_redacted &&
         !ledger_plan->raw_secret_exposed &&
         !ledger_plan->masked_text_exposed &&
         ledger_plan->submit_blocked &&
         !ledger_plan->submit_enabled &&
         !ledger_plan->auth_attempt_allowed &&
         !ledger_plan->submit_callback_bound &&
         !ledger_plan->auth_callback_bound &&
         ledger_plan->text_login_authoritative;
}

int login_window_credential_screen_journal_plan_build(
    const struct login_window_credential_screen_ledger_plan *ledger_plan,
    struct login_window_credential_screen_journal_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_journal_plan_reset(out, ledger_plan ? 1 : 0);
  if (!ledger_plan) return 0;
  out->requested_action = ledger_plan->requested_action;
  out->ledger_plan_safe = ledger_plan->ledger_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_journal_plan_ledger_is_safe(ledger_plan)) {
    out->event_type = "credential-screen-journal-plan-unsafe";
    out->blocked_reason = "credential-journal-plan-unsafe";
    out->message = "Credential screen journal plan unsafe; use text login.";
    return 0;
  }
  out->journal_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = ledger_plan->action_allowed ? 1 : 0;
  out->action_blocked = ledger_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = ledger_plan->input_focus_allowed ? 1 : 0;
  out->receipt_required = ledger_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = ledger_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = ledger_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = ledger_plan->receipt_target_selected ? 1 : 0;
  out->ledger_required = ledger_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = ledger_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = ledger_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = ledger_plan->ledger_target_selected ? 1 : 0;
  out->record_required = ledger_plan->record_required ? 1 : 0;
  out->record_allowed = ledger_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = ledger_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = ledger_plan->record_target_selected ? 1 : 0;
  out->audit_required = ledger_plan->audit_required ? 1 : 0;
  out->audit_allowed = ledger_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = ledger_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = ledger_plan->audit_target_selected ? 1 : 0;
  out->seal_required = ledger_plan->seal_required ? 1 : 0;
  out->seal_allowed = ledger_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = ledger_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = ledger_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = ledger_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = ledger_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = ledger_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = ledger_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = ledger_plan->retire_required ? 1 : 0;
  out->retire_allowed = ledger_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = ledger_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = ledger_plan->retire_target_selected ? 1 : 0;
  out->ack_required = ledger_plan->ack_required ? 1 : 0;
  out->ack_allowed = ledger_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = ledger_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = ledger_plan->ack_target_selected ? 1 : 0;
  out->completion_required = ledger_plan->completion_required ? 1 : 0;
  out->completion_allowed = ledger_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = ledger_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = ledger_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = ledger_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = ledger_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = ledger_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = ledger_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = ledger_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = ledger_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = ledger_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = ledger_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = ledger_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = ledger_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = ledger_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = ledger_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = ledger_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = ledger_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = ledger_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = ledger_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume = ledger_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback = ledger_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_credential_panel = ledger_plan->ledger_credential_panel ? 1 : 0;
  out->ledger_credential_input = ledger_plan->ledger_credential_input ? 1 : 0;
  out->ledger_credential_focus = ledger_plan->ledger_credential_focus ? 1 : 0;
  out->ledger_text_recovery = ledger_plan->ledger_text_recovery ? 1 : 0;
  out->ledger_text_login = ledger_plan->ledger_text_login ? 1 : 0;
  out->ledger_text_login_resume = ledger_plan->ledger_text_login_resume ? 1 : 0;
  out->ledger_text_login_fallback = ledger_plan->ledger_text_login_fallback ? 1 : 0;
  out->ledger_error = 0;
  out->journal_allowed = 1;
  out->journal_ticket_selected = 1;
  out->journal_target_selected = 1;
  out->journal_error = 0;
  out->recovery_text_session_required = ledger_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = ledger_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = ledger_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = ledger_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = ledger_plan->view;
  out->widget_tree = ledger_plan->widget_tree;
  out->record_ticket = ledger_plan->record_ticket;
  out->receipt_ticket = ledger_plan->receipt_ticket;
  out->ledger_ticket = ledger_plan->ledger_ticket;
  out->focus_target = ledger_plan->focus_target;
  out->primary_action = ledger_plan->primary_action;
  out->route = ledger_plan->route;
  out->compositor_target = ledger_plan->compositor_target;
  out->record_policy = ledger_plan->record_policy;
  out->receipt_policy = ledger_plan->receipt_policy;
  out->ledger_policy = ledger_plan->ledger_policy;
  out->journal_policy = "declarative-journal-no-persist";
  out->event_type = "credential-screen-journal-plan-ready";
  out->state = "journal-ready";
  out->message = "Credential screen journal ticket ready; no journal persisted.";
  out->blocked_reason = ledger_plan->blocked_reason;
  if (ledger_plan->submit_requested) {
    out->journal_ticket = "text-login-fallback-journal-ticket";
    out->compositor_target = "text-login-fallback-journal";
    out->journal_policy = "fallback-journal-declarative";
    out->journal_text_login = 1;
    out->journal_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "journal-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (ledger_plan->ledger_credential_panel && ledger_plan->ledger_credential_input &&
      ledger_plan->ledger_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->journal_ticket = "credential-screen-journal-ticket";
    out->compositor_target = "credential-screen-journal";
    out->journal_credential_panel = 1;
    out->journal_credential_input = 1;
    out->journal_credential_focus = 1;
    out->journal_text_login = 0;
    out->journal_text_login_fallback = 0;
    out->state = "journal-credential-ready";
    out->message = "Credential journal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (ledger_plan->ledger_text_recovery && out->recovery_text_session_required) {
    out->journal_ticket = "text-recovery-journal-ticket";
    out->compositor_target = "text-recovery-journal";
    out->journal_text_recovery = 1;
    out->journal_text_login = 1;
    out->journal_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "journal-text-recovery-ready";
    out->message = "Text recovery journal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (ledger_plan->ledger_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->journal_ticket = "text-login-resume-journal-ticket";
    out->compositor_target = "text-login-resume-journal";
    out->journal_policy = "full-journal-declarative";
    out->journal_text_login = 1;
    out->journal_text_login_resume = 1;
    out->journal_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "journal-resume-ready";
    out->message = "Text login resume journal ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->journal_ticket = "text-login-fallback-journal-ticket";
  out->compositor_target = "text-login-fallback-journal";
  out->journal_policy = "fallback-journal-declarative";
  out->journal_text_login = 1;
  out->journal_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "journal-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
