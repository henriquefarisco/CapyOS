/*
 * src/auth/login_runtime/tombstone_plan.c
 *
 * Credential-screen tombstone plan reset + safety predicate + copy
 * helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.43 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * tombstone stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_tombstone_plan_reset (static)
 *   - login_window_credential_screen_tombstone_plan_purge_is_safe (static)
 *   - login_window_credential_screen_tombstone_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_tombstone_plan_build
 *
 * The tombstone-plan converts a fail-closed purge-plan into a
 * tombstone contract for the downstream compaction stage.  The
 * static `_purge_is_safe` predicate consolidates the upstream-
 * safety check and `_copy_safe_fields` performs the field-by-field
 * propagation used when the upstream contract is safe.  All helpers
 * are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_tombstone_plan_reset(
    struct login_window_credential_screen_tombstone_plan *out,
    int purge_plan_available) {
  *out = (struct login_window_credential_screen_tombstone_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_TOMBSTONE_PLAN_VERSION;
  out->purge_plan_available = purge_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->receipt_required = 1;
  out->ledger_required = 1;
  out->journal_required = 1;
  out->archive_required = 1;
  out->retention_required = 1;
  out->expiry_required = 1;
  out->purge_required = 1;
  out->tombstone_required = 1;
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
  out->tombstone_text_login = 1;
  out->tombstone_text_login_fallback = 1;
  out->tombstone_error = 1;
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
  out->tombstone_ticket = "text-login-fallback-tombstone-ticket";
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
  out->tombstone_policy = "tombstone-disabled";
  out->event_type = "credential-screen-tombstone-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "purge-plan-unavailable";
}

static int login_window_credential_screen_tombstone_plan_purge_is_safe(
    const struct login_window_credential_screen_purge_plan *purge_plan) {
  return purge_plan->purge_plan_safe &&
         purge_plan->purge_required &&
         purge_plan->purge_allowed &&
         !purge_plan->purge_submitted &&
         purge_plan->purge_ticket_selected &&
         purge_plan->purge_target_selected &&
         !purge_plan->purge_persist_allowed &&
         !purge_plan->purge_persisted &&
         !purge_plan->purge_cpu_gpu_sync_allowed &&
         !purge_plan->purge_cpu_gpu_sync_submitted &&
         !purge_plan->purge_delete_allowed &&
         !purge_plan->purge_deleted &&
         !purge_plan->purge_error &&
         purge_plan->expiry_required &&
         purge_plan->expiry_allowed &&
         !purge_plan->expiry_submitted &&
         purge_plan->expiry_ticket_selected &&
         purge_plan->expiry_target_selected &&
         !purge_plan->expiry_persist_allowed &&
         !purge_plan->expiry_persisted &&
         !purge_plan->expiry_cpu_gpu_sync_allowed &&
         !purge_plan->expiry_cpu_gpu_sync_submitted &&
         !purge_plan->expiry_timer_allowed &&
         !purge_plan->expiry_timer_armed &&
         !purge_plan->expiry_delete_allowed &&
         !purge_plan->expiry_deleted &&
         !purge_plan->expiry_error &&
         purge_plan->retention_required &&
         purge_plan->retention_allowed &&
         !purge_plan->retention_submitted &&
         purge_plan->retention_ticket_selected &&
         purge_plan->retention_target_selected &&
         !purge_plan->retention_persist_allowed &&
         !purge_plan->retention_persisted &&
         !purge_plan->retention_cpu_gpu_sync_allowed &&
         !purge_plan->retention_cpu_gpu_sync_submitted &&
         !purge_plan->retention_error &&
         purge_plan->archive_required &&
         purge_plan->archive_allowed &&
         !purge_plan->archive_submitted &&
         purge_plan->archive_ticket_selected &&
         purge_plan->archive_target_selected &&
         !purge_plan->archive_persist_allowed &&
         !purge_plan->archive_persisted &&
         !purge_plan->archive_cpu_gpu_sync_allowed &&
         !purge_plan->archive_cpu_gpu_sync_submitted &&
         !purge_plan->archive_error &&
         purge_plan->journal_required &&
         purge_plan->journal_allowed &&
         !purge_plan->journal_submitted &&
         purge_plan->journal_ticket_selected &&
         purge_plan->journal_target_selected &&
         !purge_plan->journal_persist_allowed &&
         !purge_plan->journal_persisted &&
         !purge_plan->journal_cpu_gpu_sync_allowed &&
         !purge_plan->journal_cpu_gpu_sync_submitted &&
         !purge_plan->journal_error &&
         purge_plan->ledger_required &&
         purge_plan->ledger_allowed &&
         !purge_plan->ledger_submitted &&
         purge_plan->ledger_ticket_selected &&
         purge_plan->ledger_target_selected &&
         !purge_plan->ledger_persist_allowed &&
         !purge_plan->ledger_persisted &&
         !purge_plan->ledger_cpu_gpu_sync_allowed &&
         !purge_plan->ledger_cpu_gpu_sync_submitted &&
         !purge_plan->ledger_error &&
         purge_plan->receipt_required &&
         purge_plan->receipt_allowed &&
         !purge_plan->receipt_submitted &&
         purge_plan->receipt_ticket_selected &&
         purge_plan->receipt_target_selected &&
         !purge_plan->receipt_persist_allowed &&
         !purge_plan->receipt_persisted &&
         !purge_plan->receipt_cpu_gpu_sync_allowed &&
         !purge_plan->receipt_cpu_gpu_sync_submitted &&
         !purge_plan->receipt_error &&
         purge_plan->record_required &&
         purge_plan->record_allowed &&
         !purge_plan->record_submitted &&
         purge_plan->record_ticket_selected &&
         purge_plan->record_target_selected &&
         !purge_plan->record_persist_allowed &&
         !purge_plan->record_persisted &&
         !purge_plan->record_cpu_gpu_sync_allowed &&
         !purge_plan->record_cpu_gpu_sync_submitted &&
         !purge_plan->record_error &&
         purge_plan->audit_required &&
         purge_plan->audit_allowed &&
         !purge_plan->audit_submitted &&
         purge_plan->audit_ticket_selected &&
         purge_plan->audit_target_selected &&
         !purge_plan->audit_log_append_allowed &&
         !purge_plan->audit_log_appended &&
         !purge_plan->audit_cpu_gpu_sync_allowed &&
         !purge_plan->audit_cpu_gpu_sync_submitted &&
         purge_plan->seal_required &&
         purge_plan->seal_allowed &&
         !purge_plan->seal_submitted &&
         purge_plan->seal_ticket_selected &&
         purge_plan->seal_target_selected &&
         !purge_plan->seal_state_write_allowed &&
         !purge_plan->seal_state_written &&
         !purge_plan->seal_cpu_gpu_sync_allowed &&
         !purge_plan->seal_cpu_gpu_sync_submitted &&
         purge_plan->cleanup_required &&
         purge_plan->cleanup_allowed &&
         !purge_plan->cleanup_submitted &&
         purge_plan->cleanup_ticket_selected &&
         purge_plan->cleanup_target_selected &&
         !purge_plan->cleanup_resource_release_allowed &&
         !purge_plan->cleanup_resource_released &&
         !purge_plan->cleanup_cpu_gpu_sync_allowed &&
         !purge_plan->cleanup_cpu_gpu_sync_submitted &&
         purge_plan->retire_required &&
         purge_plan->retire_allowed &&
         !purge_plan->retire_submitted &&
         purge_plan->retire_ticket_selected &&
         purge_plan->retire_target_selected &&
         !purge_plan->retire_resource_release_allowed &&
         !purge_plan->retire_resource_released &&
         !purge_plan->retire_cpu_gpu_sync_allowed &&
         !purge_plan->retire_cpu_gpu_sync_submitted &&
         purge_plan->ack_required &&
         purge_plan->ack_allowed &&
         !purge_plan->ack_submitted &&
         purge_plan->ack_ticket_selected &&
         purge_plan->ack_target_selected &&
         !purge_plan->ack_cpu_gpu_sync_allowed &&
         !purge_plan->ack_cpu_gpu_sync_submitted &&
         purge_plan->completion_required &&
         purge_plan->completion_allowed &&
         purge_plan->completion_report_required &&
         !purge_plan->completion_reported &&
         purge_plan->completion_ack_required &&
         !purge_plan->completion_acknowledged &&
         purge_plan->completion_ticket_selected &&
         purge_plan->completion_target_selected &&
         !purge_plan->completion_cpu_gpu_sync_allowed &&
         !purge_plan->completion_cpu_gpu_sync_submitted &&
         purge_plan->deadline_required &&
         purge_plan->deadline_allowed &&
         !purge_plan->deadline_armed &&
         purge_plan->deadline_timer_required &&
         !purge_plan->deadline_timer_armed &&
         !purge_plan->deadline_expired &&
         purge_plan->deadline_completion_required &&
         !purge_plan->deadline_completion_reported &&
         !purge_plan->deadline_cpu_gpu_sync_allowed &&
         !purge_plan->deadline_cpu_gpu_sync_submitted &&
         !purge_plan->sync_submitted &&
         !purge_plan->sync_wait_allowed &&
         !purge_plan->sync_wait_submitted &&
         !purge_plan->sync_signal_allowed &&
         !purge_plan->sync_signal_submitted &&
         !purge_plan->sync_deadline_armed &&
         !purge_plan->sync_completion_reported &&
         !purge_plan->sync_cpu_gpu_sync_allowed &&
         !purge_plan->sync_cpu_gpu_sync_submitted &&
         !purge_plan->timeline_submitted &&
         !purge_plan->timeline_wait_allowed &&
         !purge_plan->timeline_wait_submitted &&
         !purge_plan->timeline_signal_allowed &&
         !purge_plan->timeline_signal_submitted &&
         !purge_plan->timeline_semaphore_allowed &&
         !purge_plan->timeline_semaphore_submitted &&
         !purge_plan->timeline_value_allocated &&
         !purge_plan->timeline_value_published &&
         !purge_plan->timeline_cpu_gpu_sync_allowed &&
         !purge_plan->timeline_cpu_gpu_sync_submitted &&
         !purge_plan->fence_submitted &&
         !purge_plan->fence_wait_allowed &&
         !purge_plan->fence_wait_submitted &&
         !purge_plan->fence_signal_allowed &&
         !purge_plan->fence_signal_submitted &&
         !purge_plan->fence_fd_export_allowed &&
         !purge_plan->fence_fd_exported &&
         !purge_plan->fence_cpu_gpu_sync_allowed &&
         !purge_plan->fence_cpu_gpu_sync_submitted &&
         !purge_plan->barrier_submitted &&
         !purge_plan->barrier_memory_visibility_established &&
         !purge_plan->barrier_cache_visibility_established &&
         !purge_plan->barrier_cpu_gpu_sync_allowed &&
         !purge_plan->barrier_cpu_gpu_sync_submitted &&
         !purge_plan->flush_submitted &&
         !purge_plan->flush_cache_clean_allowed &&
         !purge_plan->flush_cache_cleaned &&
         !purge_plan->flush_memory_barrier_allowed &&
         !purge_plan->flush_memory_barrier_submitted &&
         !purge_plan->framebuffer_submitted &&
         !purge_plan->framebuffer_mapped &&
         !purge_plan->framebuffer_write_allowed &&
         !purge_plan->framebuffer_written &&
         !purge_plan->framebuffer_flushed &&
         !purge_plan->framebuffer_cache_cleaned &&
         !purge_plan->blit_submitted &&
         !purge_plan->blit_source_buffer_mapped &&
         !purge_plan->blit_destination_buffer_mapped &&
         !purge_plan->blit_pixels_copied &&
         !purge_plan->blit_dma_allowed &&
         !purge_plan->blit_dma_submitted &&
         !purge_plan->output_submitted &&
         !purge_plan->output_buffer_attached &&
         !purge_plan->output_buffer_submitted &&
         !purge_plan->output_flip_allowed &&
         !purge_plan->output_flip_submitted &&
         !purge_plan->display_submitted &&
         !purge_plan->display_buffer_attached &&
         !purge_plan->display_buffer_submitted &&
         !purge_plan->display_mode_committed &&
         !purge_plan->display_flip_allowed &&
         !purge_plan->display_flip_submitted &&
         !purge_plan->scanout_submitted &&
         !purge_plan->scanout_buffer_attached &&
         !purge_plan->scanout_buffer_submitted &&
         !purge_plan->vsync_submitted &&
         !purge_plan->vsync_wait_submitted &&
         !purge_plan->vsync_fence_armed &&
         !purge_plan->schedule_submitted &&
         !purge_plan->present_submitted &&
         !purge_plan->damage_submitted &&
         !purge_plan->compositor_damage_submitted &&
         !purge_plan->frame_timer_armed &&
         !purge_plan->compositor_wake_allowed &&
         !purge_plan->compositor_wake_submitted &&
         !purge_plan->page_flip_allowed &&
         !purge_plan->page_flip_submitted &&
         purge_plan->route_selected &&
         !purge_plan->route_blocked &&
         purge_plan->credential_session_safe &&
         purge_plan->credential_storage_wiped &&
         purge_plan->credential_redacted &&
         purge_plan->length_redacted &&
         !purge_plan->raw_secret_exposed &&
         !purge_plan->masked_text_exposed &&
         purge_plan->submit_blocked &&
         !purge_plan->submit_enabled &&
         !purge_plan->auth_attempt_allowed &&
         !purge_plan->submit_callback_bound &&
         !purge_plan->auth_callback_bound &&
         purge_plan->text_login_authoritative;
}

static void login_window_credential_screen_tombstone_plan_copy_safe_fields(
    const struct login_window_credential_screen_purge_plan *purge_plan,
    struct login_window_credential_screen_tombstone_plan *out) {
  out->receipt_required = purge_plan->receipt_required ? 1 : 0;
  out->receipt_allowed = purge_plan->receipt_allowed ? 1 : 0;
  out->receipt_ticket_selected = purge_plan->receipt_ticket_selected ? 1 : 0;
  out->receipt_target_selected = purge_plan->receipt_target_selected ? 1 : 0;
  out->receipt_error = 0;
  out->ledger_required = purge_plan->ledger_required ? 1 : 0;
  out->ledger_allowed = purge_plan->ledger_allowed ? 1 : 0;
  out->ledger_ticket_selected = purge_plan->ledger_ticket_selected ? 1 : 0;
  out->ledger_target_selected = purge_plan->ledger_target_selected ? 1 : 0;
  out->ledger_error = 0;
  out->journal_required = purge_plan->journal_required ? 1 : 0;
  out->journal_allowed = purge_plan->journal_allowed ? 1 : 0;
  out->journal_ticket_selected = purge_plan->journal_ticket_selected ? 1 : 0;
  out->journal_target_selected = purge_plan->journal_target_selected ? 1 : 0;
  out->journal_error = 0;
  out->archive_required = purge_plan->archive_required ? 1 : 0;
  out->archive_allowed = purge_plan->archive_allowed ? 1 : 0;
  out->archive_ticket_selected = purge_plan->archive_ticket_selected ? 1 : 0;
  out->archive_target_selected = purge_plan->archive_target_selected ? 1 : 0;
  out->archive_error = 0;
  out->retention_required = purge_plan->retention_required ? 1 : 0;
  out->retention_allowed = purge_plan->retention_allowed ? 1 : 0;
  out->retention_ticket_selected = purge_plan->retention_ticket_selected ? 1 : 0;
  out->retention_target_selected = purge_plan->retention_target_selected ? 1 : 0;
  out->retention_error = 0;
  out->expiry_required = purge_plan->expiry_required ? 1 : 0;
  out->expiry_allowed = purge_plan->expiry_allowed ? 1 : 0;
  out->expiry_ticket_selected = purge_plan->expiry_ticket_selected ? 1 : 0;
  out->expiry_target_selected = purge_plan->expiry_target_selected ? 1 : 0;
  out->expiry_error = 0;
  out->purge_required = purge_plan->purge_required ? 1 : 0;
  out->purge_allowed = purge_plan->purge_allowed ? 1 : 0;
  out->purge_ticket_selected = purge_plan->purge_ticket_selected ? 1 : 0;
  out->purge_target_selected = purge_plan->purge_target_selected ? 1 : 0;
  out->purge_error = 0;
  out->record_required = purge_plan->record_required ? 1 : 0;
  out->record_allowed = purge_plan->record_allowed ? 1 : 0;
  out->record_ticket_selected = purge_plan->record_ticket_selected ? 1 : 0;
  out->record_target_selected = purge_plan->record_target_selected ? 1 : 0;
  out->record_error = 0;
  out->audit_required = purge_plan->audit_required ? 1 : 0;
  out->audit_allowed = purge_plan->audit_allowed ? 1 : 0;
  out->audit_ticket_selected = purge_plan->audit_ticket_selected ? 1 : 0;
  out->audit_target_selected = purge_plan->audit_target_selected ? 1 : 0;
  out->seal_required = purge_plan->seal_required ? 1 : 0;
  out->seal_allowed = purge_plan->seal_allowed ? 1 : 0;
  out->seal_ticket_selected = purge_plan->seal_ticket_selected ? 1 : 0;
  out->seal_target_selected = purge_plan->seal_target_selected ? 1 : 0;
  out->cleanup_required = purge_plan->cleanup_required ? 1 : 0;
  out->cleanup_allowed = purge_plan->cleanup_allowed ? 1 : 0;
  out->cleanup_ticket_selected = purge_plan->cleanup_ticket_selected ? 1 : 0;
  out->cleanup_target_selected = purge_plan->cleanup_target_selected ? 1 : 0;
  out->retire_required = purge_plan->retire_required ? 1 : 0;
  out->retire_allowed = purge_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = purge_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = purge_plan->retire_target_selected ? 1 : 0;
  out->ack_required = purge_plan->ack_required ? 1 : 0;
  out->ack_allowed = purge_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = purge_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = purge_plan->ack_target_selected ? 1 : 0;
  out->completion_required = purge_plan->completion_required ? 1 : 0;
  out->completion_allowed = purge_plan->completion_allowed ? 1 : 0;
  out->completion_report_required =
      purge_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required =
      purge_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected =
      purge_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected =
      purge_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = purge_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = purge_plan->deadline_allowed ? 1 : 0;
  out->deadline_timer_required = purge_plan->deadline_timer_required ? 1 : 0;
  out->deadline_completion_required =
      purge_plan->deadline_completion_required ? 1 : 0;
  out->recovery_text_session_required =
      purge_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = purge_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      purge_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = purge_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = purge_plan->view;
  out->widget_tree = purge_plan->widget_tree;
  out->record_ticket = purge_plan->record_ticket;
  out->receipt_ticket = purge_plan->receipt_ticket;
  out->ledger_ticket = purge_plan->ledger_ticket;
  out->journal_ticket = purge_plan->journal_ticket;
  out->archive_ticket = purge_plan->archive_ticket;
  out->retention_ticket = purge_plan->retention_ticket;
  out->expiry_ticket = purge_plan->expiry_ticket;
  out->purge_ticket = purge_plan->purge_ticket;
  out->focus_target = purge_plan->focus_target;
  out->primary_action = purge_plan->primary_action;
  out->route = purge_plan->route;
  out->record_policy = purge_plan->record_policy;
  out->receipt_policy = purge_plan->receipt_policy;
  out->ledger_policy = purge_plan->ledger_policy;
  out->journal_policy = purge_plan->journal_policy;
  out->archive_policy = purge_plan->archive_policy;
  out->retention_policy = purge_plan->retention_policy;
  out->expiry_policy = purge_plan->expiry_policy;
  out->purge_policy = purge_plan->purge_policy;
}

int login_window_credential_screen_tombstone_plan_build(
    const struct login_window_credential_screen_purge_plan *purge_plan,
    struct login_window_credential_screen_tombstone_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_tombstone_plan_reset(out, purge_plan ? 1 : 0);
  if (!purge_plan) return 0;
  out->requested_action = purge_plan->requested_action;
  out->purge_plan_safe = purge_plan->purge_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_tombstone_plan_purge_is_safe(purge_plan)) {
    out->event_type = "credential-screen-tombstone-plan-unsafe";
    out->blocked_reason = "credential-tombstone-plan-unsafe";
    out->message = "Credential screen tombstone plan unsafe; use text login.";
    return 0;
  }
  out->tombstone_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = purge_plan->action_allowed ? 1 : 0;
  out->action_blocked = purge_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = purge_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_tombstone_plan_copy_safe_fields(purge_plan, out);
  out->tombstone_allowed = 1;
  out->tombstone_ticket_selected = 1;
  out->tombstone_target_selected = 1;
  out->tombstone_error = 0;
  out->tombstone_policy = "declarative-tombstone-no-write";
  out->event_type = "credential-screen-tombstone-plan-ready";
  out->state = "tombstone-ready";
  out->message = "Credential screen tombstone ticket ready; no tombstone written.";
  out->blocked_reason = purge_plan->blocked_reason;
  if (purge_plan->submit_requested) {
    out->tombstone_ticket = "text-login-fallback-tombstone-ticket";
    out->compositor_target = "text-login-fallback-tombstone";
    out->tombstone_policy = "fallback-tombstone-declarative";
    out->tombstone_text_login = 1;
    out->tombstone_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "tombstone-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (purge_plan->purge_credential_panel &&
      purge_plan->purge_credential_input &&
      purge_plan->purge_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->tombstone_ticket = "credential-screen-tombstone-ticket";
    out->compositor_target = "credential-screen-tombstone";
    out->tombstone_credential_panel = 1;
    out->tombstone_credential_input = 1;
    out->tombstone_credential_focus = 1;
    out->tombstone_text_login = 0;
    out->tombstone_text_login_fallback = 0;
    out->state = "tombstone-credential-ready";
    out->message = "Credential tombstone ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (purge_plan->purge_text_recovery && out->recovery_text_session_required) {
    out->tombstone_ticket = "text-recovery-tombstone-ticket";
    out->compositor_target = "text-recovery-tombstone";
    out->tombstone_text_recovery = 1;
    out->tombstone_text_login = 1;
    out->tombstone_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "tombstone-text-recovery-ready";
    out->message = "Text recovery tombstone ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (purge_plan->purge_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->tombstone_ticket = "text-login-resume-tombstone-ticket";
    out->compositor_target = "text-login-resume-tombstone";
    out->tombstone_policy = "full-tombstone-declarative";
    out->tombstone_text_login = 1;
    out->tombstone_text_login_resume = 1;
    out->tombstone_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "tombstone-resume-ready";
    out->message = "Text login resume tombstone ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->tombstone_ticket = "text-login-fallback-tombstone-ticket";
  out->compositor_target = "text-login-fallback-tombstone";
  out->tombstone_policy = "fallback-tombstone-declarative";
  out->tombstone_text_login = 1;
  out->tombstone_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "tombstone-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
