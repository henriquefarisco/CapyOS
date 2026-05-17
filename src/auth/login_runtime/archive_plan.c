/*
 * src/auth/login_runtime/archive_plan.c
 *
 * Credential-screen archive plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.39 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the archive stage of the credential pipeline:
 *
 *   - login_window_credential_screen_archive_plan_reset (static)
 *   - login_window_credential_screen_archive_plan_journal_is_safe (static)
 *   - login_window_credential_screen_archive_plan_build
 *
 * The archive-plan converts a fail-closed journal-plan into an
 * archive contract for the downstream retention stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_journal_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_archive_plan_reset(
    struct login_window_credential_screen_archive_plan *out,
    int journal_plan_available) {
  *out = (struct login_window_credential_screen_archive_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_ARCHIVE_PLAN_VERSION;
  out->journal_plan_available = journal_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
  out->archive_required = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->record_policy = "record-disabled";
  out->receipt_policy = "receipt-disabled";
  out->ledger_policy = "ledger-disabled";
  out->journal_policy = "journal-disabled";
  out->archive_policy = "archive-disabled";
  out->event_type = "credential-screen-archive-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "journal-plan-unavailable";
}

static int login_window_credential_screen_archive_plan_journal_is_safe(
    const struct login_window_credential_screen_journal_plan *journal_plan) {
  return journal_plan->journal_plan_safe &&
         journal_plan->journal_required &&
         journal_plan->journal_allowed &&
         !journal_plan->journal_submitted &&
         journal_plan->journal_ticket_selected &&
         journal_plan->journal_target_selected &&
         !journal_plan->journal_persist_allowed &&
         !journal_plan->journal_persisted &&
         !journal_plan->journal_cpu_gpu_sync_allowed &&
         !journal_plan->journal_cpu_gpu_sync_submitted &&
         !journal_plan->journal_error &&
         journal_plan->ledger_required &&
         journal_plan->ledger_allowed &&
         !journal_plan->ledger_submitted &&
         journal_plan->ledger_ticket_selected &&
         journal_plan->ledger_target_selected &&
         !journal_plan->ledger_persist_allowed &&
         !journal_plan->ledger_persisted &&
         !journal_plan->ledger_cpu_gpu_sync_allowed &&
         !journal_plan->ledger_cpu_gpu_sync_submitted &&
         !journal_plan->ledger_error &&
         journal_plan->receipt_required &&
         journal_plan->receipt_allowed &&
         !journal_plan->receipt_submitted &&
         journal_plan->receipt_ticket_selected &&
         journal_plan->receipt_target_selected &&
         !journal_plan->receipt_persist_allowed &&
         !journal_plan->receipt_persisted &&
         !journal_plan->receipt_cpu_gpu_sync_allowed &&
         !journal_plan->receipt_cpu_gpu_sync_submitted &&
         !journal_plan->receipt_error &&
         journal_plan->record_required &&
         journal_plan->record_allowed &&
         !journal_plan->record_submitted &&
         journal_plan->record_ticket_selected &&
         journal_plan->record_target_selected &&
         !journal_plan->record_persist_allowed &&
         !journal_plan->record_persisted &&
         !journal_plan->record_cpu_gpu_sync_allowed &&
         !journal_plan->record_cpu_gpu_sync_submitted &&
         !journal_plan->record_error &&
         journal_plan->audit_required &&
         journal_plan->audit_allowed &&
         !journal_plan->audit_submitted &&
         journal_plan->audit_ticket_selected &&
         journal_plan->audit_target_selected &&
         !journal_plan->audit_log_append_allowed &&
         !journal_plan->audit_log_appended &&
         !journal_plan->audit_cpu_gpu_sync_allowed &&
         !journal_plan->audit_cpu_gpu_sync_submitted &&
         journal_plan->seal_required &&
         journal_plan->seal_allowed &&
         !journal_plan->seal_submitted &&
         journal_plan->seal_ticket_selected &&
         journal_plan->seal_target_selected &&
         !journal_plan->seal_state_write_allowed &&
         !journal_plan->seal_state_written &&
         !journal_plan->seal_cpu_gpu_sync_allowed &&
         !journal_plan->seal_cpu_gpu_sync_submitted &&
         journal_plan->cleanup_required &&
         journal_plan->cleanup_allowed &&
         !journal_plan->cleanup_submitted &&
         journal_plan->cleanup_ticket_selected &&
         journal_plan->cleanup_target_selected &&
         !journal_plan->cleanup_resource_release_allowed &&
         !journal_plan->cleanup_resource_released &&
         !journal_plan->cleanup_cpu_gpu_sync_allowed &&
         !journal_plan->cleanup_cpu_gpu_sync_submitted &&
         journal_plan->retire_required &&
         journal_plan->retire_allowed &&
         !journal_plan->retire_submitted &&
         journal_plan->retire_ticket_selected &&
         journal_plan->retire_target_selected &&
         !journal_plan->retire_resource_release_allowed &&
         !journal_plan->retire_resource_released &&
         !journal_plan->retire_cpu_gpu_sync_allowed &&
         !journal_plan->retire_cpu_gpu_sync_submitted &&
         journal_plan->ack_required &&
         journal_plan->ack_allowed &&
         !journal_plan->ack_submitted &&
         journal_plan->ack_ticket_selected &&
         journal_plan->ack_target_selected &&
         !journal_plan->ack_cpu_gpu_sync_allowed &&
         !journal_plan->ack_cpu_gpu_sync_submitted &&
         journal_plan->completion_required &&
         journal_plan->completion_allowed &&
         journal_plan->completion_report_required &&
         !journal_plan->completion_reported &&
         journal_plan->completion_ack_required &&
         !journal_plan->completion_acknowledged &&
         journal_plan->completion_ticket_selected &&
         journal_plan->completion_target_selected &&
         !journal_plan->completion_cpu_gpu_sync_allowed &&
         !journal_plan->completion_cpu_gpu_sync_submitted &&
         journal_plan->deadline_required &&
         journal_plan->deadline_allowed &&
         !journal_plan->deadline_armed &&
         journal_plan->deadline_timer_required &&
         !journal_plan->deadline_timer_armed &&
         !journal_plan->deadline_expired &&
         journal_plan->deadline_completion_required &&
         !journal_plan->deadline_completion_reported &&
         !journal_plan->deadline_cpu_gpu_sync_allowed &&
         !journal_plan->deadline_cpu_gpu_sync_submitted &&
         !journal_plan->sync_submitted &&
         !journal_plan->sync_wait_allowed &&
         !journal_plan->sync_wait_submitted &&
         !journal_plan->sync_signal_allowed &&
         !journal_plan->sync_signal_submitted &&
         !journal_plan->sync_deadline_armed &&
         !journal_plan->sync_completion_reported &&
         !journal_plan->sync_cpu_gpu_sync_allowed &&
         !journal_plan->sync_cpu_gpu_sync_submitted &&
         !journal_plan->timeline_submitted &&
         !journal_plan->timeline_wait_allowed &&
         !journal_plan->timeline_wait_submitted &&
         !journal_plan->timeline_signal_allowed &&
         !journal_plan->timeline_signal_submitted &&
         !journal_plan->timeline_semaphore_allowed &&
         !journal_plan->timeline_semaphore_submitted &&
         !journal_plan->timeline_value_allocated &&
         !journal_plan->timeline_value_published &&
         !journal_plan->timeline_cpu_gpu_sync_allowed &&
         !journal_plan->timeline_cpu_gpu_sync_submitted &&
         !journal_plan->fence_submitted &&
         !journal_plan->fence_wait_allowed &&
         !journal_plan->fence_wait_submitted &&
         !journal_plan->fence_signal_allowed &&
         !journal_plan->fence_signal_submitted &&
         !journal_plan->fence_fd_export_allowed &&
         !journal_plan->fence_fd_exported &&
         !journal_plan->fence_cpu_gpu_sync_allowed &&
         !journal_plan->fence_cpu_gpu_sync_submitted &&
         !journal_plan->barrier_submitted &&
         !journal_plan->barrier_memory_visibility_established &&
         !journal_plan->barrier_cache_visibility_established &&
         !journal_plan->barrier_cpu_gpu_sync_allowed &&
         !journal_plan->barrier_cpu_gpu_sync_submitted &&
         !journal_plan->flush_submitted &&
         !journal_plan->flush_cache_clean_allowed &&
         !journal_plan->flush_cache_cleaned &&
         !journal_plan->flush_memory_barrier_allowed &&
         !journal_plan->flush_memory_barrier_submitted &&
         !journal_plan->framebuffer_submitted &&
         !journal_plan->framebuffer_mapped &&
         !journal_plan->framebuffer_write_allowed &&
         !journal_plan->framebuffer_written &&
         !journal_plan->framebuffer_flushed &&
         !journal_plan->framebuffer_cache_cleaned &&
         !journal_plan->blit_submitted &&
         !journal_plan->blit_source_buffer_mapped &&
         !journal_plan->blit_destination_buffer_mapped &&
         !journal_plan->blit_pixels_copied &&
         !journal_plan->blit_dma_allowed &&
         !journal_plan->blit_dma_submitted &&
         !journal_plan->output_submitted &&
         !journal_plan->output_buffer_attached &&
         !journal_plan->output_buffer_submitted &&
         !journal_plan->output_flip_allowed &&
         !journal_plan->output_flip_submitted &&
         !journal_plan->display_submitted &&
         !journal_plan->display_buffer_attached &&
         !journal_plan->display_buffer_submitted &&
         !journal_plan->display_mode_committed &&
         !journal_plan->display_flip_allowed &&
         !journal_plan->display_flip_submitted &&
         !journal_plan->scanout_submitted &&
         !journal_plan->scanout_buffer_attached &&
         !journal_plan->scanout_buffer_submitted &&
         !journal_plan->vsync_submitted &&
         !journal_plan->vsync_wait_submitted &&
         !journal_plan->vsync_fence_armed &&
         !journal_plan->schedule_submitted &&
         !journal_plan->present_submitted &&
         !journal_plan->damage_submitted &&
         !journal_plan->compositor_damage_submitted &&
         !journal_plan->frame_timer_armed &&
         !journal_plan->compositor_wake_allowed &&
         !journal_plan->compositor_wake_submitted &&
         !journal_plan->page_flip_allowed &&
         !journal_plan->page_flip_submitted &&
         journal_plan->route_selected &&
         !journal_plan->route_blocked &&
         journal_plan->credential_session_safe &&
         journal_plan->credential_storage_wiped &&
         journal_plan->credential_redacted &&
         journal_plan->length_redacted &&
         !journal_plan->raw_secret_exposed &&
         !journal_plan->masked_text_exposed &&
         journal_plan->submit_blocked &&
         !journal_plan->submit_enabled &&
         !journal_plan->auth_attempt_allowed &&
         !journal_plan->submit_callback_bound &&
         !journal_plan->auth_callback_bound &&
         journal_plan->text_login_authoritative;
}

int login_window_credential_screen_archive_plan_build(
    const struct login_window_credential_screen_journal_plan *journal_plan,
    struct login_window_credential_screen_archive_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_archive_plan_reset(out, journal_plan ? 1 : 0);
  if (!journal_plan) return 0;
  out->requested_action = journal_plan->requested_action;
  out->journal_plan_safe = journal_plan->journal_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_archive_plan_journal_is_safe(journal_plan)) {
    out->event_type = "credential-screen-archive-plan-unsafe";
    out->blocked_reason = "credential-archive-plan-unsafe";
    out->message = "Credential screen archive plan unsafe; use text login.";
    return 0;
  }
  out->archive_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = journal_plan->action_allowed ? 1 : 0;
  out->action_blocked = journal_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = journal_plan->input_focus_allowed ? 1 : 0;
  out->receipt_required = journal_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = journal_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = journal_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = journal_plan->receipt_target_selected ? 1 : 0;
  out->ledger_required = journal_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = journal_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = journal_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = journal_plan->ledger_target_selected ? 1 : 0;
  out->journal_required = journal_plan->journal_required ? 1 : 0;
  out->journal_allowed = journal_plan->journal_allowed ? 1 : 0;
  out->journal_ticket_selected = journal_plan->journal_ticket_selected ? 1 : 0;
  out->journal_target_selected = journal_plan->journal_target_selected ? 1 : 0;
  out->record_required = journal_plan->record_required ? 1 : 0;
  out->record_allowed = journal_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = journal_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = journal_plan->record_target_selected ? 1 : 0;
  out->audit_required = journal_plan->audit_required ? 1 : 0;
  out->audit_allowed = journal_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = journal_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = journal_plan->audit_target_selected ? 1 : 0;
  out->seal_required = journal_plan->seal_required ? 1 : 0;
  out->seal_allowed = journal_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = journal_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = journal_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = journal_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = journal_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = journal_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = journal_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = journal_plan->retire_required ? 1 : 0;
  out->retire_allowed = journal_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = journal_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = journal_plan->retire_target_selected ? 1 : 0;
  out->ack_required = journal_plan->ack_required ? 1 : 0;
  out->ack_allowed = journal_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = journal_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = journal_plan->ack_target_selected ? 1 : 0;
  out->completion_required = journal_plan->completion_required ? 1 : 0;
  out->completion_allowed = journal_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = journal_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = journal_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = journal_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = journal_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = journal_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = journal_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = journal_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = journal_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = journal_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = journal_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = journal_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = journal_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = journal_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = journal_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = journal_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = journal_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = journal_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = journal_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume = journal_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback = journal_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_credential_panel = journal_plan->ledger_credential_panel ? 1 : 0;
  out->ledger_credential_input = journal_plan->ledger_credential_input ? 1 : 0;
  out->ledger_credential_focus = journal_plan->ledger_credential_focus ? 1 : 0;
  out->ledger_text_recovery = journal_plan->ledger_text_recovery ? 1 : 0;
  out->ledger_text_login = journal_plan->ledger_text_login ? 1 : 0;
  out->ledger_text_login_resume = journal_plan->ledger_text_login_resume ? 1 : 0;
  out->ledger_text_login_fallback = journal_plan->ledger_text_login_fallback ? 1 : 0;
  out->ledger_error = 0;
  out->journal_credential_panel = journal_plan->journal_credential_panel ? 1 : 0;
  out->journal_credential_input = journal_plan->journal_credential_input ? 1 : 0;
  out->journal_credential_focus = journal_plan->journal_credential_focus ? 1 : 0;
  out->journal_text_recovery = journal_plan->journal_text_recovery ? 1 : 0;
  out->journal_text_login = journal_plan->journal_text_login ? 1 : 0;
  out->journal_text_login_resume = journal_plan->journal_text_login_resume ? 1 : 0;
  out->journal_text_login_fallback = journal_plan->journal_text_login_fallback ? 1 : 0;
  out->journal_error = 0;
  out->archive_allowed = 1;
  out->archive_ticket_selected = 1;
  out->archive_target_selected = 1;
  out->archive_error = 0;
  out->recovery_text_session_required = journal_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = journal_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = journal_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = journal_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = journal_plan->view;
  out->widget_tree = journal_plan->widget_tree;
  out->record_ticket = journal_plan->record_ticket;
  out->receipt_ticket = journal_plan->receipt_ticket;
  out->ledger_ticket = journal_plan->ledger_ticket;
  out->journal_ticket = journal_plan->journal_ticket;
  out->focus_target = journal_plan->focus_target;
  out->primary_action = journal_plan->primary_action;
  out->route = journal_plan->route;
  out->compositor_target = journal_plan->compositor_target;
  out->record_policy = journal_plan->record_policy;
  out->receipt_policy = journal_plan->receipt_policy;
  out->ledger_policy = journal_plan->ledger_policy;
  out->journal_policy = journal_plan->journal_policy;
  out->archive_policy = "declarative-archive-no-persist";
  out->event_type = "credential-screen-archive-plan-ready";
  out->state = "archive-ready";
  out->message = "Credential screen archive ticket ready; no archive persisted.";
  out->blocked_reason = journal_plan->blocked_reason;
  if (journal_plan->submit_requested) {
    out->archive_ticket = "text-login-fallback-archive-ticket";
    out->compositor_target = "text-login-fallback-archive";
    out->archive_policy = "fallback-archive-declarative";
    out->archive_text_login = 1;
    out->archive_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "archive-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (journal_plan->journal_credential_panel && journal_plan->journal_credential_input &&
      journal_plan->journal_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->archive_ticket = "credential-screen-archive-ticket";
    out->compositor_target = "credential-screen-archive";
    out->archive_credential_panel = 1;
    out->archive_credential_input = 1;
    out->archive_credential_focus = 1;
    out->archive_text_login = 0;
    out->archive_text_login_fallback = 0;
    out->state = "archive-credential-ready";
    out->message = "Credential archive ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (journal_plan->journal_text_recovery && out->recovery_text_session_required) {
    out->archive_ticket = "text-recovery-archive-ticket";
    out->compositor_target = "text-recovery-archive";
    out->archive_text_recovery = 1;
    out->archive_text_login = 1;
    out->archive_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "archive-text-recovery-ready";
    out->message = "Text recovery archive ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (journal_plan->journal_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->archive_ticket = "text-login-resume-archive-ticket";
    out->compositor_target = "text-login-resume-archive";
    out->archive_policy = "full-archive-declarative";
    out->archive_text_login = 1;
    out->archive_text_login_resume = 1;
    out->archive_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "archive-resume-ready";
    out->message = "Text login resume archive ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->archive_ticket = "text-login-fallback-archive-ticket";
  out->compositor_target = "text-login-fallback-archive";
  out->archive_policy = "fallback-archive-declarative";
  out->archive_text_login = 1;
  out->archive_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "archive-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
