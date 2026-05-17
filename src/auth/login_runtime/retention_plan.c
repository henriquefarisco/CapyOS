/*
 * src/auth/login_runtime/retention_plan.c
 *
 * Credential-screen retention plan reset + safety helper + builder
 * — extracted byte-for-byte from `src/auth/login_runtime.c` during
 * PR C.40 of the Estagio C dedicated plan.  Hosts the per-plan
 * reset helper, the upstream-safety predicate and the public
 * builder for the retention stage of the credential pipeline:
 *
 *   - login_window_credential_screen_retention_plan_reset (static)
 *   - login_window_credential_screen_retention_plan_archive_is_safe (static)
 *   - login_window_credential_screen_retention_plan_build
 *
 * The retention-plan converts a fail-closed archive-plan into a
 * retention contract for the downstream expiry stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_archive_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_retention_plan_reset(
    struct login_window_credential_screen_retention_plan *out,
    int archive_plan_available) {
  *out = (struct login_window_credential_screen_retention_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RETENTION_PLAN_VERSION;
  out->archive_plan_available = archive_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
  out->archive_required = 1;
  out->retention_required = 1;
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
  out->event_type = "credential-screen-retention-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "archive-plan-unavailable";
}

static int login_window_credential_screen_retention_plan_archive_is_safe(
    const struct login_window_credential_screen_archive_plan *archive_plan) {
  return archive_plan->archive_plan_safe &&
         archive_plan->archive_required &&
         archive_plan->archive_allowed &&
         !archive_plan->archive_submitted &&
         archive_plan->archive_ticket_selected &&
         archive_plan->archive_target_selected &&
         !archive_plan->archive_persist_allowed &&
         !archive_plan->archive_persisted &&
         !archive_plan->archive_cpu_gpu_sync_allowed &&
         !archive_plan->archive_cpu_gpu_sync_submitted &&
         !archive_plan->archive_error &&
         archive_plan->journal_required &&
         archive_plan->journal_allowed &&
         !archive_plan->journal_submitted &&
         archive_plan->journal_ticket_selected &&
         archive_plan->journal_target_selected &&
         !archive_plan->journal_persist_allowed &&
         !archive_plan->journal_persisted &&
         !archive_plan->journal_cpu_gpu_sync_allowed &&
         !archive_plan->journal_cpu_gpu_sync_submitted &&
         !archive_plan->journal_error &&
         archive_plan->ledger_required &&
         archive_plan->ledger_allowed &&
         !archive_plan->ledger_submitted &&
         archive_plan->ledger_ticket_selected &&
         archive_plan->ledger_target_selected &&
         !archive_plan->ledger_persist_allowed &&
         !archive_plan->ledger_persisted &&
         !archive_plan->ledger_cpu_gpu_sync_allowed &&
         !archive_plan->ledger_cpu_gpu_sync_submitted &&
         !archive_plan->ledger_error &&
         archive_plan->receipt_required &&
         archive_plan->receipt_allowed &&
         !archive_plan->receipt_submitted &&
         archive_plan->receipt_ticket_selected &&
         archive_plan->receipt_target_selected &&
         !archive_plan->receipt_persist_allowed &&
         !archive_plan->receipt_persisted &&
         !archive_plan->receipt_cpu_gpu_sync_allowed &&
         !archive_plan->receipt_cpu_gpu_sync_submitted &&
         !archive_plan->receipt_error &&
         archive_plan->record_required &&
         archive_plan->record_allowed &&
         !archive_plan->record_submitted &&
         archive_plan->record_ticket_selected &&
         archive_plan->record_target_selected &&
         !archive_plan->record_persist_allowed &&
         !archive_plan->record_persisted &&
         !archive_plan->record_cpu_gpu_sync_allowed &&
         !archive_plan->record_cpu_gpu_sync_submitted &&
         !archive_plan->record_error &&
         archive_plan->audit_required &&
         archive_plan->audit_allowed &&
         !archive_plan->audit_submitted &&
         archive_plan->audit_ticket_selected &&
         archive_plan->audit_target_selected &&
         !archive_plan->audit_log_append_allowed &&
         !archive_plan->audit_log_appended &&
         !archive_plan->audit_cpu_gpu_sync_allowed &&
         !archive_plan->audit_cpu_gpu_sync_submitted &&
         archive_plan->seal_required &&
         archive_plan->seal_allowed &&
         !archive_plan->seal_submitted &&
         archive_plan->seal_ticket_selected &&
         archive_plan->seal_target_selected &&
         !archive_plan->seal_state_write_allowed &&
         !archive_plan->seal_state_written &&
         !archive_plan->seal_cpu_gpu_sync_allowed &&
         !archive_plan->seal_cpu_gpu_sync_submitted &&
         archive_plan->cleanup_required &&
         archive_plan->cleanup_allowed &&
         !archive_plan->cleanup_submitted &&
         archive_plan->cleanup_ticket_selected &&
         archive_plan->cleanup_target_selected &&
         !archive_plan->cleanup_resource_release_allowed &&
         !archive_plan->cleanup_resource_released &&
         !archive_plan->cleanup_cpu_gpu_sync_allowed &&
         !archive_plan->cleanup_cpu_gpu_sync_submitted &&
         archive_plan->retire_required &&
         archive_plan->retire_allowed &&
         !archive_plan->retire_submitted &&
         archive_plan->retire_ticket_selected &&
         archive_plan->retire_target_selected &&
         !archive_plan->retire_resource_release_allowed &&
         !archive_plan->retire_resource_released &&
         !archive_plan->retire_cpu_gpu_sync_allowed &&
         !archive_plan->retire_cpu_gpu_sync_submitted &&
         archive_plan->ack_required &&
         archive_plan->ack_allowed &&
         !archive_plan->ack_submitted &&
         archive_plan->ack_ticket_selected &&
         archive_plan->ack_target_selected &&
         !archive_plan->ack_cpu_gpu_sync_allowed &&
         !archive_plan->ack_cpu_gpu_sync_submitted &&
         archive_plan->completion_required &&
         archive_plan->completion_allowed &&
         archive_plan->completion_report_required &&
         !archive_plan->completion_reported &&
         archive_plan->completion_ack_required &&
         !archive_plan->completion_acknowledged &&
         archive_plan->completion_ticket_selected &&
         archive_plan->completion_target_selected &&
         !archive_plan->completion_cpu_gpu_sync_allowed &&
         !archive_plan->completion_cpu_gpu_sync_submitted &&
         archive_plan->deadline_required &&
         archive_plan->deadline_allowed &&
         !archive_plan->deadline_armed &&
         archive_plan->deadline_timer_required &&
         !archive_plan->deadline_timer_armed &&
         !archive_plan->deadline_expired &&
         archive_plan->deadline_completion_required &&
         !archive_plan->deadline_completion_reported &&
         !archive_plan->deadline_cpu_gpu_sync_allowed &&
         !archive_plan->deadline_cpu_gpu_sync_submitted &&
         !archive_plan->sync_submitted &&
         !archive_plan->sync_wait_allowed &&
         !archive_plan->sync_wait_submitted &&
         !archive_plan->sync_signal_allowed &&
         !archive_plan->sync_signal_submitted &&
         !archive_plan->sync_deadline_armed &&
         !archive_plan->sync_completion_reported &&
         !archive_plan->sync_cpu_gpu_sync_allowed &&
         !archive_plan->sync_cpu_gpu_sync_submitted &&
         !archive_plan->timeline_submitted &&
         !archive_plan->timeline_wait_allowed &&
         !archive_plan->timeline_wait_submitted &&
         !archive_plan->timeline_signal_allowed &&
         !archive_plan->timeline_signal_submitted &&
         !archive_plan->timeline_semaphore_allowed &&
         !archive_plan->timeline_semaphore_submitted &&
         !archive_plan->timeline_value_allocated &&
         !archive_plan->timeline_value_published &&
         !archive_plan->timeline_cpu_gpu_sync_allowed &&
         !archive_plan->timeline_cpu_gpu_sync_submitted &&
         !archive_plan->fence_submitted &&
         !archive_plan->fence_wait_allowed &&
         !archive_plan->fence_wait_submitted &&
         !archive_plan->fence_signal_allowed &&
         !archive_plan->fence_signal_submitted &&
         !archive_plan->fence_fd_export_allowed &&
         !archive_plan->fence_fd_exported &&
         !archive_plan->fence_cpu_gpu_sync_allowed &&
         !archive_plan->fence_cpu_gpu_sync_submitted &&
         !archive_plan->barrier_submitted &&
         !archive_plan->barrier_memory_visibility_established &&
         !archive_plan->barrier_cache_visibility_established &&
         !archive_plan->barrier_cpu_gpu_sync_allowed &&
         !archive_plan->barrier_cpu_gpu_sync_submitted &&
         !archive_plan->flush_submitted &&
         !archive_plan->flush_cache_clean_allowed &&
         !archive_plan->flush_cache_cleaned &&
         !archive_plan->flush_memory_barrier_allowed &&
         !archive_plan->flush_memory_barrier_submitted &&
         !archive_plan->framebuffer_submitted &&
         !archive_plan->framebuffer_mapped &&
         !archive_plan->framebuffer_write_allowed &&
         !archive_plan->framebuffer_written &&
         !archive_plan->framebuffer_flushed &&
         !archive_plan->framebuffer_cache_cleaned &&
         !archive_plan->blit_submitted &&
         !archive_plan->blit_source_buffer_mapped &&
         !archive_plan->blit_destination_buffer_mapped &&
         !archive_plan->blit_pixels_copied &&
         !archive_plan->blit_dma_allowed &&
         !archive_plan->blit_dma_submitted &&
         !archive_plan->output_submitted &&
         !archive_plan->output_buffer_attached &&
         !archive_plan->output_buffer_submitted &&
         !archive_plan->output_flip_allowed &&
         !archive_plan->output_flip_submitted &&
         !archive_plan->display_submitted &&
         !archive_plan->display_buffer_attached &&
         !archive_plan->display_buffer_submitted &&
         !archive_plan->display_mode_committed &&
         !archive_plan->display_flip_allowed &&
         !archive_plan->display_flip_submitted &&
         !archive_plan->scanout_submitted &&
         !archive_plan->scanout_buffer_attached &&
         !archive_plan->scanout_buffer_submitted &&
         !archive_plan->vsync_submitted &&
         !archive_plan->vsync_wait_submitted &&
         !archive_plan->vsync_fence_armed &&
         !archive_plan->schedule_submitted &&
         !archive_plan->present_submitted &&
         !archive_plan->damage_submitted &&
         !archive_plan->compositor_damage_submitted &&
         !archive_plan->frame_timer_armed &&
         !archive_plan->compositor_wake_allowed &&
         !archive_plan->compositor_wake_submitted &&
         !archive_plan->page_flip_allowed &&
         !archive_plan->page_flip_submitted &&
         archive_plan->route_selected &&
         !archive_plan->route_blocked &&
         archive_plan->credential_session_safe &&
         archive_plan->credential_storage_wiped &&
         archive_plan->credential_redacted &&
         archive_plan->length_redacted &&
         !archive_plan->raw_secret_exposed &&
         !archive_plan->masked_text_exposed &&
         archive_plan->submit_blocked &&
         !archive_plan->submit_enabled &&
         !archive_plan->auth_attempt_allowed &&
         !archive_plan->submit_callback_bound &&
         !archive_plan->auth_callback_bound &&
         archive_plan->text_login_authoritative;
}

int login_window_credential_screen_retention_plan_build(
    const struct login_window_credential_screen_archive_plan *archive_plan,
    struct login_window_credential_screen_retention_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_retention_plan_reset(out, archive_plan ? 1 : 0);
  if (!archive_plan) return 0;
  out->requested_action = archive_plan->requested_action;
  out->archive_plan_safe = archive_plan->archive_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_retention_plan_archive_is_safe(archive_plan)) {
    out->event_type = "credential-screen-retention-plan-unsafe";
    out->blocked_reason = "credential-retention-plan-unsafe";
    out->message = "Credential screen retention plan unsafe; use text login.";
    return 0;
  }
  out->retention_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = archive_plan->action_allowed ? 1 : 0;
  out->action_blocked = archive_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = archive_plan->input_focus_allowed ? 1 : 0;
  out->receipt_required = archive_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = archive_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = archive_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = archive_plan->receipt_target_selected ? 1 : 0;
  out->ledger_required = archive_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = archive_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = archive_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = archive_plan->ledger_target_selected ? 1 : 0;
  out->journal_required = archive_plan->journal_required ? 1 : 0;
  out->journal_allowed = archive_plan->journal_allowed ? 1 : 0;
  out->journal_ticket_selected = archive_plan->journal_ticket_selected ? 1 : 0;
  out->journal_target_selected = archive_plan->journal_target_selected ? 1 : 0;
  out->archive_required = archive_plan->archive_required ? 1 : 0;
  out->archive_allowed = archive_plan->archive_allowed ? 1 : 0;
  out->archive_ticket_selected = archive_plan->archive_ticket_selected ? 1 : 0;
  out->archive_target_selected = archive_plan->archive_target_selected ? 1 : 0;
  out->record_required = archive_plan->record_required ? 1 : 0;
  out->record_allowed = archive_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = archive_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = archive_plan->record_target_selected ? 1 : 0;
  out->audit_required = archive_plan->audit_required ? 1 : 0;
  out->audit_allowed = archive_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = archive_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = archive_plan->audit_target_selected ? 1 : 0;
  out->seal_required = archive_plan->seal_required ? 1 : 0;
  out->seal_allowed = archive_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = archive_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = archive_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = archive_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = archive_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = archive_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = archive_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = archive_plan->retire_required ? 1 : 0;
  out->retire_allowed = archive_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = archive_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = archive_plan->retire_target_selected ? 1 : 0;
  out->ack_required = archive_plan->ack_required ? 1 : 0;
  out->ack_allowed = archive_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = archive_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = archive_plan->ack_target_selected ? 1 : 0;
  out->completion_required = archive_plan->completion_required ? 1 : 0;
  out->completion_allowed = archive_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = archive_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = archive_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = archive_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = archive_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = archive_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = archive_plan->deadline_allowed ? 1 : 0;
  out->record_credential_panel = archive_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = archive_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = archive_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = archive_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = archive_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = archive_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback = archive_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = archive_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = archive_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = archive_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = archive_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = archive_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume = archive_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback = archive_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_credential_panel = archive_plan->ledger_credential_panel ? 1 : 0;
  out->ledger_credential_input = archive_plan->ledger_credential_input ? 1 : 0;
  out->ledger_credential_focus = archive_plan->ledger_credential_focus ? 1 : 0;
  out->ledger_text_recovery = archive_plan->ledger_text_recovery ? 1 : 0;
  out->ledger_text_login = archive_plan->ledger_text_login ? 1 : 0;
  out->ledger_text_login_resume = archive_plan->ledger_text_login_resume ? 1 : 0;
  out->ledger_text_login_fallback = archive_plan->ledger_text_login_fallback ? 1 : 0;
  out->ledger_error = 0;
  out->journal_credential_panel = archive_plan->journal_credential_panel ? 1 : 0;
  out->journal_credential_input = archive_plan->journal_credential_input ? 1 : 0;
  out->journal_credential_focus = archive_plan->journal_credential_focus ? 1 : 0;
  out->journal_text_recovery = archive_plan->journal_text_recovery ? 1 : 0;
  out->journal_text_login = archive_plan->journal_text_login ? 1 : 0;
  out->journal_text_login_resume = archive_plan->journal_text_login_resume ? 1 : 0;
  out->journal_text_login_fallback = archive_plan->journal_text_login_fallback ? 1 : 0;
  out->journal_error = 0;
  out->archive_credential_panel = archive_plan->archive_credential_panel ? 1 : 0;
  out->archive_credential_input = archive_plan->archive_credential_input ? 1 : 0;
  out->archive_credential_focus = archive_plan->archive_credential_focus ? 1 : 0;
  out->archive_text_recovery = archive_plan->archive_text_recovery ? 1 : 0;
  out->archive_text_login = archive_plan->archive_text_login ? 1 : 0;
  out->archive_text_login_resume = archive_plan->archive_text_login_resume ? 1 : 0;
  out->archive_text_login_fallback = archive_plan->archive_text_login_fallback ? 1 : 0;
  out->archive_error = 0;
  out->retention_allowed = 1;
  out->retention_ticket_selected = 1;
  out->retention_target_selected = 1;
  out->retention_error = 0;
  out->recovery_text_session_required = archive_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = archive_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = archive_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = archive_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = archive_plan->view;
  out->widget_tree = archive_plan->widget_tree;
  out->record_ticket = archive_plan->record_ticket;
  out->receipt_ticket = archive_plan->receipt_ticket;
  out->ledger_ticket = archive_plan->ledger_ticket;
  out->journal_ticket = archive_plan->journal_ticket;
  out->archive_ticket = archive_plan->archive_ticket;
  out->focus_target = archive_plan->focus_target;
  out->primary_action = archive_plan->primary_action;
  out->route = archive_plan->route;
  out->compositor_target = archive_plan->compositor_target;
  out->record_policy = archive_plan->record_policy;
  out->receipt_policy = archive_plan->receipt_policy;
  out->ledger_policy = archive_plan->ledger_policy;
  out->journal_policy = archive_plan->journal_policy;
  out->archive_policy = archive_plan->archive_policy;
  out->retention_policy = "declarative-retention-no-persist";
  out->event_type = "credential-screen-retention-plan-ready";
  out->state = "retention-ready";
  out->message = "Credential screen retention ticket ready; no retention persisted.";
  out->blocked_reason = archive_plan->blocked_reason;
  if (archive_plan->submit_requested) {
    out->retention_ticket = "text-login-fallback-retention-ticket";
    out->compositor_target = "text-login-fallback-retention";
    out->retention_policy = "fallback-retention-declarative";
    out->retention_text_login = 1;
    out->retention_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "retention-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (archive_plan->archive_credential_panel && archive_plan->archive_credential_input &&
      archive_plan->archive_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->retention_ticket = "credential-screen-retention-ticket";
    out->compositor_target = "credential-screen-retention";
    out->retention_credential_panel = 1;
    out->retention_credential_input = 1;
    out->retention_credential_focus = 1;
    out->retention_text_login = 0;
    out->retention_text_login_fallback = 0;
    out->state = "retention-credential-ready";
    out->message = "Credential retention ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (archive_plan->archive_text_recovery && out->recovery_text_session_required) {
    out->retention_ticket = "text-recovery-retention-ticket";
    out->compositor_target = "text-recovery-retention";
    out->retention_text_recovery = 1;
    out->retention_text_login = 1;
    out->retention_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "retention-text-recovery-ready";
    out->message = "Text recovery retention ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (archive_plan->archive_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->retention_ticket = "text-login-resume-retention-ticket";
    out->compositor_target = "text-login-resume-retention";
    out->retention_policy = "full-retention-declarative";
    out->retention_text_login = 1;
    out->retention_text_login_resume = 1;
    out->retention_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "retention-resume-ready";
    out->message = "Text login resume retention ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->retention_ticket = "text-login-fallback-retention-ticket";
  out->compositor_target = "text-login-fallback-retention";
  out->retention_policy = "fallback-retention-declarative";
  out->retention_text_login = 1;
  out->retention_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "retention-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
