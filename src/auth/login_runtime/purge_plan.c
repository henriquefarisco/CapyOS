/*
 * src/auth/login_runtime/purge_plan.c
 *
 * Credential-screen purge plan reset + safety predicates + copy
 * helpers + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.42 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * purge stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_purge_plan_reset (static)
 *   - login_window_credential_screen_purge_plan_chain_is_safe (static)
 *   - login_window_credential_screen_purge_plan_lifecycle_is_safe (static)
 *   - login_window_credential_screen_purge_plan_compositor_is_safe (static)
 *   - login_window_credential_screen_purge_plan_credential_is_safe (static)
 *   - login_window_credential_screen_purge_plan_expiry_is_safe (static)
 *   - login_window_credential_screen_purge_plan_copy_chain (static)
 *   - login_window_credential_screen_purge_plan_copy_routes (static)
 *   - login_window_credential_screen_purge_plan_copy_strings (static)
 *   - login_window_credential_screen_purge_plan_build
 *
 * The purge-plan converts a fail-closed expiry-plan into a purge
 * contract for the downstream tombstone stage.  The static helpers
 * decompose the upstream-safety check into four orthogonal
 * predicates (chain/lifecycle/compositor/credential) and the
 * field-copy logic into three orthogonal copy helpers (chain
 * fields, route flags and string pointers).  All helpers are kept
 * file-local so they cannot be reused outside this translation
 * unit; the public builder is the only entry point.  Opens Phase 5
 * of the Estagio C dedicated plan.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_purge_plan_reset(
    struct login_window_credential_screen_purge_plan *out,
    int expiry_plan_available) {
  *out = (struct login_window_credential_screen_purge_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_PURGE_PLAN_VERSION;
  out->expiry_plan_available = expiry_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
  out->archive_required = 1;
  out->retention_required = 1;
  out->expiry_required = 1;
  out->purge_required = 1;
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
  out->purge_text_login = 1;
  out->purge_text_login_fallback = 1;
  out->purge_error = 1;
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
  out->purge_ticket = "text-login-fallback-purge-ticket";
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
  out->purge_policy = "purge-disabled";
  out->event_type = "credential-screen-purge-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "expiry-plan-unavailable";
}

static int login_window_credential_screen_purge_plan_chain_is_safe(
    const struct login_window_credential_screen_expiry_plan *expiry_plan) {
  return expiry_plan->expiry_plan_safe &&
         expiry_plan->expiry_required &&
         expiry_plan->expiry_allowed &&
         !expiry_plan->expiry_submitted &&
         expiry_plan->expiry_ticket_selected &&
         expiry_plan->expiry_target_selected &&
         !expiry_plan->expiry_persist_allowed &&
         !expiry_plan->expiry_persisted &&
         !expiry_plan->expiry_cpu_gpu_sync_allowed &&
         !expiry_plan->expiry_cpu_gpu_sync_submitted &&
         !expiry_plan->expiry_timer_allowed &&
         !expiry_plan->expiry_timer_armed &&
         !expiry_plan->expiry_delete_allowed &&
         !expiry_plan->expiry_deleted &&
         !expiry_plan->expiry_error &&
         expiry_plan->retention_required &&
         expiry_plan->retention_allowed &&
         !expiry_plan->retention_submitted &&
         expiry_plan->retention_ticket_selected &&
         expiry_plan->retention_target_selected &&
         !expiry_plan->retention_persist_allowed &&
         !expiry_plan->retention_persisted &&
         !expiry_plan->retention_cpu_gpu_sync_allowed &&
         !expiry_plan->retention_cpu_gpu_sync_submitted &&
         !expiry_plan->retention_error &&
         expiry_plan->archive_required &&
         expiry_plan->archive_allowed &&
         !expiry_plan->archive_submitted &&
         expiry_plan->archive_ticket_selected &&
         expiry_plan->archive_target_selected &&
         !expiry_plan->archive_persist_allowed &&
         !expiry_plan->archive_persisted &&
         !expiry_plan->archive_cpu_gpu_sync_allowed &&
         !expiry_plan->archive_cpu_gpu_sync_submitted &&
         !expiry_plan->archive_error &&
         expiry_plan->journal_required &&
         expiry_plan->journal_allowed &&
         !expiry_plan->journal_submitted &&
         expiry_plan->journal_ticket_selected &&
         expiry_plan->journal_target_selected &&
         !expiry_plan->journal_persist_allowed &&
         !expiry_plan->journal_persisted &&
         !expiry_plan->journal_cpu_gpu_sync_allowed &&
         !expiry_plan->journal_cpu_gpu_sync_submitted &&
         !expiry_plan->journal_error &&
         expiry_plan->ledger_required &&
         expiry_plan->ledger_allowed &&
         !expiry_plan->ledger_submitted &&
         expiry_plan->ledger_ticket_selected &&
         expiry_plan->ledger_target_selected &&
         !expiry_plan->ledger_persist_allowed &&
         !expiry_plan->ledger_persisted &&
         !expiry_plan->ledger_cpu_gpu_sync_allowed &&
         !expiry_plan->ledger_cpu_gpu_sync_submitted &&
         !expiry_plan->ledger_error &&
         expiry_plan->receipt_required &&
         expiry_plan->receipt_allowed &&
         !expiry_plan->receipt_submitted &&
         expiry_plan->receipt_ticket_selected &&
         expiry_plan->receipt_target_selected &&
         !expiry_plan->receipt_persist_allowed &&
         !expiry_plan->receipt_persisted &&
         !expiry_plan->receipt_cpu_gpu_sync_allowed &&
         !expiry_plan->receipt_cpu_gpu_sync_submitted &&
         !expiry_plan->receipt_error &&
         expiry_plan->record_required &&
         expiry_plan->record_allowed &&
         !expiry_plan->record_submitted &&
         expiry_plan->record_ticket_selected &&
         expiry_plan->record_target_selected &&
         !expiry_plan->record_persist_allowed &&
         !expiry_plan->record_persisted &&
         !expiry_plan->record_cpu_gpu_sync_allowed &&
         !expiry_plan->record_cpu_gpu_sync_submitted &&
         !expiry_plan->record_error;
}

static int login_window_credential_screen_purge_plan_lifecycle_is_safe(
    const struct login_window_credential_screen_expiry_plan *expiry_plan) {
  return expiry_plan->audit_required &&
         expiry_plan->audit_allowed &&
         !expiry_plan->audit_submitted &&
         expiry_plan->audit_ticket_selected &&
         expiry_plan->audit_target_selected &&
         !expiry_plan->audit_log_append_allowed &&
         !expiry_plan->audit_log_appended &&
         !expiry_plan->audit_cpu_gpu_sync_allowed &&
         !expiry_plan->audit_cpu_gpu_sync_submitted &&
         expiry_plan->seal_required &&
         expiry_plan->seal_allowed &&
         !expiry_plan->seal_submitted &&
         expiry_plan->seal_ticket_selected &&
         expiry_plan->seal_target_selected &&
         !expiry_plan->seal_state_write_allowed &&
         !expiry_plan->seal_state_written &&
         !expiry_plan->seal_cpu_gpu_sync_allowed &&
         !expiry_plan->seal_cpu_gpu_sync_submitted &&
         expiry_plan->cleanup_required &&
         expiry_plan->cleanup_allowed &&
         !expiry_plan->cleanup_submitted &&
         expiry_plan->cleanup_ticket_selected &&
         expiry_plan->cleanup_target_selected &&
         !expiry_plan->cleanup_resource_release_allowed &&
         !expiry_plan->cleanup_resource_released &&
         !expiry_plan->cleanup_cpu_gpu_sync_allowed &&
         !expiry_plan->cleanup_cpu_gpu_sync_submitted &&
         expiry_plan->retire_required &&
         expiry_plan->retire_allowed &&
         !expiry_plan->retire_submitted &&
         expiry_plan->retire_ticket_selected &&
         expiry_plan->retire_target_selected &&
         !expiry_plan->retire_resource_release_allowed &&
         !expiry_plan->retire_resource_released &&
         !expiry_plan->retire_cpu_gpu_sync_allowed &&
         !expiry_plan->retire_cpu_gpu_sync_submitted &&
         expiry_plan->ack_required &&
         expiry_plan->ack_allowed &&
         !expiry_plan->ack_submitted &&
         expiry_plan->ack_ticket_selected &&
         expiry_plan->ack_target_selected &&
         !expiry_plan->ack_cpu_gpu_sync_allowed &&
         !expiry_plan->ack_cpu_gpu_sync_submitted &&
         expiry_plan->completion_required &&
         expiry_plan->completion_allowed &&
         expiry_plan->completion_report_required &&
         !expiry_plan->completion_reported &&
         expiry_plan->completion_ack_required &&
         !expiry_plan->completion_acknowledged &&
         expiry_plan->completion_ticket_selected &&
         expiry_plan->completion_target_selected &&
         !expiry_plan->completion_cpu_gpu_sync_allowed &&
         !expiry_plan->completion_cpu_gpu_sync_submitted &&
         expiry_plan->deadline_required &&
         expiry_plan->deadline_allowed &&
         !expiry_plan->deadline_armed &&
         expiry_plan->deadline_timer_required &&
         !expiry_plan->deadline_timer_armed &&
         !expiry_plan->deadline_expired &&
         expiry_plan->deadline_completion_required &&
         !expiry_plan->deadline_completion_reported &&
         !expiry_plan->deadline_cpu_gpu_sync_allowed &&
         !expiry_plan->deadline_cpu_gpu_sync_submitted;
}

static int login_window_credential_screen_purge_plan_compositor_is_safe(
    const struct login_window_credential_screen_expiry_plan *expiry_plan) {
  return !expiry_plan->sync_submitted &&
         !expiry_plan->sync_wait_allowed &&
         !expiry_plan->sync_wait_submitted &&
         !expiry_plan->sync_signal_allowed &&
         !expiry_plan->sync_signal_submitted &&
         !expiry_plan->sync_deadline_armed &&
         !expiry_plan->sync_completion_reported &&
         !expiry_plan->sync_cpu_gpu_sync_allowed &&
         !expiry_plan->sync_cpu_gpu_sync_submitted &&
         !expiry_plan->timeline_submitted &&
         !expiry_plan->timeline_wait_allowed &&
         !expiry_plan->timeline_wait_submitted &&
         !expiry_plan->timeline_signal_allowed &&
         !expiry_plan->timeline_signal_submitted &&
         !expiry_plan->timeline_semaphore_allowed &&
         !expiry_plan->timeline_semaphore_submitted &&
         !expiry_plan->timeline_value_allocated &&
         !expiry_plan->timeline_value_published &&
         !expiry_plan->timeline_cpu_gpu_sync_allowed &&
         !expiry_plan->timeline_cpu_gpu_sync_submitted &&
         !expiry_plan->fence_submitted &&
         !expiry_plan->fence_wait_allowed &&
         !expiry_plan->fence_wait_submitted &&
         !expiry_plan->fence_signal_allowed &&
         !expiry_plan->fence_signal_submitted &&
         !expiry_plan->fence_fd_export_allowed &&
         !expiry_plan->fence_fd_exported &&
         !expiry_plan->fence_cpu_gpu_sync_allowed &&
         !expiry_plan->fence_cpu_gpu_sync_submitted &&
         !expiry_plan->barrier_submitted &&
         !expiry_plan->barrier_memory_visibility_established &&
         !expiry_plan->barrier_cache_visibility_established &&
         !expiry_plan->barrier_cpu_gpu_sync_allowed &&
         !expiry_plan->barrier_cpu_gpu_sync_submitted &&
         !expiry_plan->flush_submitted &&
         !expiry_plan->flush_cache_clean_allowed &&
         !expiry_plan->flush_cache_cleaned &&
         !expiry_plan->flush_memory_barrier_allowed &&
         !expiry_plan->flush_memory_barrier_submitted &&
         !expiry_plan->framebuffer_submitted &&
         !expiry_plan->framebuffer_mapped &&
         !expiry_plan->framebuffer_write_allowed &&
         !expiry_plan->framebuffer_written &&
         !expiry_plan->framebuffer_flushed &&
         !expiry_plan->framebuffer_cache_cleaned &&
         !expiry_plan->blit_submitted &&
         !expiry_plan->blit_source_buffer_mapped &&
         !expiry_plan->blit_destination_buffer_mapped &&
         !expiry_plan->blit_pixels_copied &&
         !expiry_plan->blit_dma_allowed &&
         !expiry_plan->blit_dma_submitted &&
         !expiry_plan->output_submitted &&
         !expiry_plan->output_buffer_attached &&
         !expiry_plan->output_buffer_submitted &&
         !expiry_plan->output_flip_allowed &&
         !expiry_plan->output_flip_submitted &&
         !expiry_plan->display_submitted &&
         !expiry_plan->display_buffer_attached &&
         !expiry_plan->display_buffer_submitted &&
         !expiry_plan->display_mode_committed &&
         !expiry_plan->display_flip_allowed &&
         !expiry_plan->display_flip_submitted &&
         !expiry_plan->scanout_submitted &&
         !expiry_plan->scanout_buffer_attached &&
         !expiry_plan->scanout_buffer_submitted &&
         !expiry_plan->vsync_submitted &&
         !expiry_plan->vsync_wait_submitted &&
         !expiry_plan->vsync_fence_armed &&
         !expiry_plan->schedule_submitted &&
         !expiry_plan->present_submitted &&
         !expiry_plan->damage_submitted &&
         !expiry_plan->compositor_damage_submitted &&
         !expiry_plan->frame_timer_armed &&
         !expiry_plan->compositor_wake_allowed &&
         !expiry_plan->compositor_wake_submitted &&
         !expiry_plan->page_flip_allowed &&
         !expiry_plan->page_flip_submitted;
}

static int login_window_credential_screen_purge_plan_credential_is_safe(
    const struct login_window_credential_screen_expiry_plan *expiry_plan) {
  return expiry_plan->route_selected &&
         !expiry_plan->route_blocked &&
         expiry_plan->credential_session_safe &&
         expiry_plan->credential_storage_wiped &&
         expiry_plan->credential_redacted &&
         expiry_plan->length_redacted &&
         !expiry_plan->raw_secret_exposed &&
         !expiry_plan->masked_text_exposed &&
         expiry_plan->submit_blocked &&
         !expiry_plan->submit_enabled &&
         !expiry_plan->auth_attempt_allowed &&
         !expiry_plan->submit_callback_bound &&
         !expiry_plan->auth_callback_bound &&
         expiry_plan->text_login_authoritative;
}

static int login_window_credential_screen_purge_plan_expiry_is_safe(
    const struct login_window_credential_screen_expiry_plan *expiry_plan) {
  return login_window_credential_screen_purge_plan_chain_is_safe(expiry_plan) &&
         login_window_credential_screen_purge_plan_lifecycle_is_safe(expiry_plan) &&
         login_window_credential_screen_purge_plan_compositor_is_safe(expiry_plan) &&
         login_window_credential_screen_purge_plan_credential_is_safe(expiry_plan);
}

static void login_window_credential_screen_purge_plan_copy_chain(
    const struct login_window_credential_screen_expiry_plan *expiry_plan,
    struct login_window_credential_screen_purge_plan *out) {
  out->receipt_required = expiry_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = expiry_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = expiry_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = expiry_plan->receipt_target_selected ? 1 : 0;
  out->ledger_required = expiry_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = expiry_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = expiry_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = expiry_plan->ledger_target_selected ? 1 : 0;
  out->journal_required = expiry_plan->journal_required ? 1 : 0;
  out->journal_allowed = expiry_plan->journal_allowed ? 1 : 0;
  out->journal_ticket_selected = expiry_plan->journal_ticket_selected ? 1 : 0;
  out->journal_target_selected = expiry_plan->journal_target_selected ? 1 : 0;
  out->archive_required = expiry_plan->archive_required ? 1 : 0;
  out->archive_allowed = expiry_plan->archive_allowed ? 1 : 0;
  out->archive_ticket_selected = expiry_plan->archive_ticket_selected ? 1 : 0;
  out->archive_target_selected = expiry_plan->archive_target_selected ? 1 : 0;
  out->retention_required = expiry_plan->retention_required ? 1 : 0;
  out->retention_allowed = expiry_plan->retention_allowed ? 1 : 0;
  out->retention_ticket_selected = expiry_plan->retention_ticket_selected ? 1 : 0;
  out->retention_target_selected = expiry_plan->retention_target_selected ? 1 : 0;
  out->expiry_required = expiry_plan->expiry_required ? 1 : 0;
  out->expiry_allowed = expiry_plan->expiry_allowed ? 1 : 0;
  out->expiry_ticket_selected = expiry_plan->expiry_ticket_selected ? 1 : 0;
  out->expiry_target_selected = expiry_plan->expiry_target_selected ? 1 : 0;
  out->record_required = expiry_plan->record_required ? 1 : 0;
  out->record_allowed = expiry_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = expiry_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = expiry_plan->record_target_selected ? 1 : 0;
  out->audit_required = expiry_plan->audit_required ? 1 : 0;
  out->audit_allowed = expiry_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = expiry_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = expiry_plan->audit_target_selected ? 1 : 0;
  out->seal_required = expiry_plan->seal_required ? 1 : 0;
  out->seal_allowed = expiry_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = expiry_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = expiry_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = expiry_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = expiry_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = expiry_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = expiry_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = expiry_plan->retire_required ? 1 : 0;
  out->retire_allowed = expiry_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = expiry_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = expiry_plan->retire_target_selected ? 1 : 0;
  out->ack_required = expiry_plan->ack_required ? 1 : 0;
  out->ack_allowed = expiry_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = expiry_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = expiry_plan->ack_target_selected ? 1 : 0;
  out->completion_required = expiry_plan->completion_required ? 1 : 0;
  out->completion_allowed = expiry_plan->completion_allowed ? 1 : 0;
  out->completion_report_required =
      expiry_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required =
      expiry_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected =
      expiry_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected =
      expiry_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = expiry_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = expiry_plan->deadline_allowed ? 1 : 0;
}

static void login_window_credential_screen_purge_plan_copy_routes(
    const struct login_window_credential_screen_expiry_plan *expiry_plan,
    struct login_window_credential_screen_purge_plan *out) {
  out->record_credential_panel = expiry_plan->record_credential_panel ? 1 : 0;
  out->record_credential_input = expiry_plan->record_credential_input ? 1 : 0;
  out->record_credential_focus = expiry_plan->record_credential_focus ? 1 : 0;
  out->record_text_recovery = expiry_plan->record_text_recovery ? 1 : 0;
  out->record_text_login = expiry_plan->record_text_login ? 1 : 0;
  out->record_text_login_resume = expiry_plan->record_text_login_resume ? 1 : 0;
  out->record_text_login_fallback =
      expiry_plan->record_text_login_fallback ? 1 : 0;
  out->record_error = 0;
  out->receipt_credential_panel = expiry_plan->receipt_credential_panel ? 1 : 0;
  out->receipt_credential_input = expiry_plan->receipt_credential_input ? 1 : 0;
  out->receipt_credential_focus = expiry_plan->receipt_credential_focus ? 1 : 0;
  out->receipt_text_recovery = expiry_plan->receipt_text_recovery ? 1 : 0;
  out->receipt_text_login = expiry_plan->receipt_text_login ? 1 : 0;
  out->receipt_text_login_resume =
      expiry_plan->receipt_text_login_resume ? 1 : 0;
  out->receipt_text_login_fallback =
      expiry_plan->receipt_text_login_fallback ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_credential_panel = expiry_plan->ledger_credential_panel ? 1 : 0;
  out->ledger_credential_input = expiry_plan->ledger_credential_input ? 1 : 0;
  out->ledger_credential_focus = expiry_plan->ledger_credential_focus ? 1 : 0;
  out->ledger_text_recovery = expiry_plan->ledger_text_recovery ? 1 : 0;
  out->ledger_text_login = expiry_plan->ledger_text_login ? 1 : 0;
  out->ledger_text_login_resume =
      expiry_plan->ledger_text_login_resume ? 1 : 0;
  out->ledger_text_login_fallback =
      expiry_plan->ledger_text_login_fallback ? 1 : 0;
  out->ledger_error = 0;
  out->journal_credential_panel = expiry_plan->journal_credential_panel ? 1 : 0;
  out->journal_credential_input = expiry_plan->journal_credential_input ? 1 : 0;
  out->journal_credential_focus = expiry_plan->journal_credential_focus ? 1 : 0;
  out->journal_text_recovery = expiry_plan->journal_text_recovery ? 1 : 0;
  out->journal_text_login = expiry_plan->journal_text_login ? 1 : 0;
  out->journal_text_login_resume =
      expiry_plan->journal_text_login_resume ? 1 : 0;
  out->journal_text_login_fallback =
      expiry_plan->journal_text_login_fallback ? 1 : 0;
  out->journal_error = 0;
  out->archive_credential_panel = expiry_plan->archive_credential_panel ? 1 : 0;
  out->archive_credential_input = expiry_plan->archive_credential_input ? 1 : 0;
  out->archive_credential_focus = expiry_plan->archive_credential_focus ? 1 : 0;
  out->archive_text_recovery = expiry_plan->archive_text_recovery ? 1 : 0;
  out->archive_text_login = expiry_plan->archive_text_login ? 1 : 0;
  out->archive_text_login_resume =
      expiry_plan->archive_text_login_resume ? 1 : 0;
  out->archive_text_login_fallback =
      expiry_plan->archive_text_login_fallback ? 1 : 0;
  out->archive_error = 0;
  out->retention_credential_panel =
      expiry_plan->retention_credential_panel ? 1 : 0;
  out->retention_credential_input =
      expiry_plan->retention_credential_input ? 1 : 0;
  out->retention_credential_focus =
      expiry_plan->retention_credential_focus ? 1 : 0;
  out->retention_text_recovery = expiry_plan->retention_text_recovery ? 1 : 0;
  out->retention_text_login = expiry_plan->retention_text_login ? 1 : 0;
  out->retention_text_login_resume =
      expiry_plan->retention_text_login_resume ? 1 : 0;
  out->retention_text_login_fallback =
      expiry_plan->retention_text_login_fallback ? 1 : 0;
  out->retention_error = 0;
  out->expiry_credential_panel = expiry_plan->expiry_credential_panel ? 1 : 0;
  out->expiry_credential_input = expiry_plan->expiry_credential_input ? 1 : 0;
  out->expiry_credential_focus = expiry_plan->expiry_credential_focus ? 1 : 0;
  out->expiry_text_recovery = expiry_plan->expiry_text_recovery ? 1 : 0;
  out->expiry_text_login = expiry_plan->expiry_text_login ? 1 : 0;
  out->expiry_text_login_resume =
      expiry_plan->expiry_text_login_resume ? 1 : 0;
  out->expiry_text_login_fallback =
      expiry_plan->expiry_text_login_fallback ? 1 : 0;
  out->expiry_error = 0;
}

static void login_window_credential_screen_purge_plan_copy_strings(
    const struct login_window_credential_screen_expiry_plan *expiry_plan,
    struct login_window_credential_screen_purge_plan *out) {
  out->view = expiry_plan->view;
  out->widget_tree = expiry_plan->widget_tree;
  out->record_ticket = expiry_plan->record_ticket;
  out->receipt_ticket = expiry_plan->receipt_ticket;
  out->ledger_ticket = expiry_plan->ledger_ticket;
  out->journal_ticket = expiry_plan->journal_ticket;
  out->archive_ticket = expiry_plan->archive_ticket;
  out->retention_ticket = expiry_plan->retention_ticket;
  out->expiry_ticket = expiry_plan->expiry_ticket;
  out->focus_target = expiry_plan->focus_target;
  out->primary_action = expiry_plan->primary_action;
  out->route = expiry_plan->route;
  out->compositor_target = expiry_plan->compositor_target;
  out->record_policy = expiry_plan->record_policy;
  out->receipt_policy = expiry_plan->receipt_policy;
  out->ledger_policy = expiry_plan->ledger_policy;
  out->journal_policy = expiry_plan->journal_policy;
  out->archive_policy = expiry_plan->archive_policy;
  out->retention_policy = expiry_plan->retention_policy;
  out->expiry_policy = expiry_plan->expiry_policy;
}

int login_window_credential_screen_purge_plan_build(
    const struct login_window_credential_screen_expiry_plan *expiry_plan,
    struct login_window_credential_screen_purge_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_purge_plan_reset(out, expiry_plan ? 1 : 0);
  if (!expiry_plan) return 0;
  out->requested_action = expiry_plan->requested_action;
  out->expiry_plan_safe = expiry_plan->expiry_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_purge_plan_expiry_is_safe(expiry_plan)) {
    out->event_type = "credential-screen-purge-plan-unsafe";
    out->blocked_reason = "credential-purge-plan-unsafe";
    out->message = "Credential screen purge plan unsafe; use text login.";
    return 0;
  }
  out->purge_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = expiry_plan->action_allowed ? 1 : 0;
  out->action_blocked = expiry_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = expiry_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_purge_plan_copy_chain(expiry_plan, out);
  login_window_credential_screen_purge_plan_copy_routes(expiry_plan, out);
  login_window_credential_screen_purge_plan_copy_strings(expiry_plan, out);
  out->purge_allowed = 1;
  out->purge_ticket_selected = 1;
  out->purge_target_selected = 1;
  out->purge_error = 0;
  out->recovery_text_session_required =
      expiry_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = expiry_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      expiry_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = expiry_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->purge_policy = "declarative-purge-no-delete";
  out->event_type = "credential-screen-purge-plan-ready";
  out->state = "purge-ready";
  out->message = "Credential screen purge ticket ready; no purge executed.";
  out->blocked_reason = expiry_plan->blocked_reason;
  if (expiry_plan->submit_requested) {
    out->purge_ticket = "text-login-fallback-purge-ticket";
    out->compositor_target = "text-login-fallback-purge";
    out->purge_policy = "fallback-purge-declarative";
    out->purge_text_login = 1;
    out->purge_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "purge-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (expiry_plan->expiry_credential_panel &&
      expiry_plan->expiry_credential_input &&
      expiry_plan->expiry_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->purge_ticket = "credential-screen-purge-ticket";
    out->compositor_target = "credential-screen-purge";
    out->purge_credential_panel = 1;
    out->purge_credential_input = 1;
    out->purge_credential_focus = 1;
    out->purge_text_login = 0;
    out->purge_text_login_fallback = 0;
    out->state = "purge-credential-ready";
    out->message = "Credential purge ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (expiry_plan->expiry_text_recovery && out->recovery_text_session_required) {
    out->purge_ticket = "text-recovery-purge-ticket";
    out->compositor_target = "text-recovery-purge";
    out->purge_text_recovery = 1;
    out->purge_text_login = 1;
    out->purge_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "purge-text-recovery-ready";
    out->message = "Text recovery purge ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (expiry_plan->expiry_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->purge_ticket = "text-login-resume-purge-ticket";
    out->compositor_target = "text-login-resume-purge";
    out->purge_policy = "full-purge-declarative";
    out->purge_text_login = 1;
    out->purge_text_login_resume = 1;
    out->purge_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "purge-resume-ready";
    out->message = "Text login resume purge ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->purge_ticket = "text-login-fallback-purge-ticket";
  out->compositor_target = "text-login-fallback-purge";
  out->purge_policy = "fallback-purge-declarative";
  out->purge_text_login = 1;
  out->purge_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "purge-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
