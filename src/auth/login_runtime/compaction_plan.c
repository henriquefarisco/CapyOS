/*
 * src/auth/login_runtime/compaction_plan.c
 *
 * Credential-screen compaction plan reset + safety predicate +
 * copy helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.44 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * compaction stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_compaction_plan_reset (static)
 *   - login_window_credential_screen_compaction_plan_tombstone_is_safe (static)
 *   - login_window_credential_screen_compaction_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_compaction_plan_build
 *
 * The compaction-plan converts a fail-closed tombstone-plan into a
 * compaction contract for the downstream reclaim stage.  The static
 * `_tombstone_is_safe` predicate consolidates the upstream-safety
 * check and `_copy_safe_fields` performs the field-by-field
 * propagation used when the upstream contract is safe.  All helpers
 * are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_compaction_plan_reset(
    struct login_window_credential_screen_compaction_plan *out,
    int tombstone_plan_available) {
  *out = (struct login_window_credential_screen_compaction_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPACTION_PLAN_VERSION;
  out->tombstone_plan_available = tombstone_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->tombstone_required = 1;
  out->compaction_required = 1;
  out->compaction_text_login = 1;
  out->compaction_text_login_fallback = 1;
  out->compaction_error = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->tombstone_ticket = "text-login-fallback-tombstone-ticket";
  out->compaction_ticket = "text-login-fallback-compaction-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->tombstone_policy = "tombstone-disabled";
  out->compaction_policy = "compaction-disabled";
  out->event_type = "credential-screen-compaction-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "tombstone-plan-unavailable";
}

static int login_window_credential_screen_compaction_plan_tombstone_is_safe(
    const struct login_window_credential_screen_tombstone_plan *tombstone_plan) {
  return tombstone_plan->tombstone_plan_safe &&
         tombstone_plan->tombstone_required &&
         tombstone_plan->tombstone_allowed &&
         !tombstone_plan->tombstone_submitted &&
         tombstone_plan->tombstone_ticket_selected &&
         tombstone_plan->tombstone_target_selected &&
         !tombstone_plan->tombstone_persist_allowed &&
         !tombstone_plan->tombstone_persisted &&
         !tombstone_plan->tombstone_cpu_gpu_sync_allowed &&
         !tombstone_plan->tombstone_cpu_gpu_sync_submitted &&
         !tombstone_plan->tombstone_error &&
         tombstone_plan->purge_required &&
         tombstone_plan->purge_allowed &&
         !tombstone_plan->purge_submitted &&
         tombstone_plan->purge_ticket_selected &&
         tombstone_plan->purge_target_selected &&
         !tombstone_plan->purge_persist_allowed &&
         !tombstone_plan->purge_persisted &&
         !tombstone_plan->purge_cpu_gpu_sync_allowed &&
         !tombstone_plan->purge_cpu_gpu_sync_submitted &&
         !tombstone_plan->purge_delete_allowed &&
         !tombstone_plan->purge_deleted &&
         !tombstone_plan->purge_error &&
         tombstone_plan->expiry_required &&
         tombstone_plan->expiry_allowed &&
         !tombstone_plan->expiry_submitted &&
         tombstone_plan->expiry_ticket_selected &&
         tombstone_plan->expiry_target_selected &&
         !tombstone_plan->expiry_persist_allowed &&
         !tombstone_plan->expiry_persisted &&
         !tombstone_plan->expiry_cpu_gpu_sync_allowed &&
         !tombstone_plan->expiry_cpu_gpu_sync_submitted &&
         !tombstone_plan->expiry_timer_allowed &&
         !tombstone_plan->expiry_timer_armed &&
         !tombstone_plan->expiry_delete_allowed &&
         !tombstone_plan->expiry_deleted &&
         !tombstone_plan->expiry_error &&
         tombstone_plan->retention_required &&
         tombstone_plan->retention_allowed &&
         !tombstone_plan->retention_submitted &&
         tombstone_plan->retention_ticket_selected &&
         tombstone_plan->retention_target_selected &&
         !tombstone_plan->retention_persist_allowed &&
         !tombstone_plan->retention_persisted &&
         !tombstone_plan->retention_cpu_gpu_sync_allowed &&
         !tombstone_plan->retention_cpu_gpu_sync_submitted &&
         !tombstone_plan->retention_error &&
         tombstone_plan->archive_required &&
         tombstone_plan->archive_allowed &&
         !tombstone_plan->archive_submitted &&
         tombstone_plan->archive_ticket_selected &&
         tombstone_plan->archive_target_selected &&
         !tombstone_plan->archive_persist_allowed &&
         !tombstone_plan->archive_persisted &&
         !tombstone_plan->archive_cpu_gpu_sync_allowed &&
         !tombstone_plan->archive_cpu_gpu_sync_submitted &&
         !tombstone_plan->archive_error &&
         tombstone_plan->journal_required &&
         tombstone_plan->journal_allowed &&
         !tombstone_plan->journal_submitted &&
         tombstone_plan->journal_ticket_selected &&
         tombstone_plan->journal_target_selected &&
         !tombstone_plan->journal_persist_allowed &&
         !tombstone_plan->journal_persisted &&
         !tombstone_plan->journal_cpu_gpu_sync_allowed &&
         !tombstone_plan->journal_cpu_gpu_sync_submitted &&
         !tombstone_plan->journal_error &&
         tombstone_plan->ledger_required &&
         tombstone_plan->ledger_allowed &&
         !tombstone_plan->ledger_submitted &&
         tombstone_plan->ledger_ticket_selected &&
         tombstone_plan->ledger_target_selected &&
         !tombstone_plan->ledger_persist_allowed &&
         !tombstone_plan->ledger_persisted &&
         !tombstone_plan->ledger_cpu_gpu_sync_allowed &&
         !tombstone_plan->ledger_cpu_gpu_sync_submitted &&
         !tombstone_plan->ledger_error &&
         tombstone_plan->receipt_required &&
         tombstone_plan->receipt_allowed &&
         !tombstone_plan->receipt_submitted &&
         tombstone_plan->receipt_ticket_selected &&
         tombstone_plan->receipt_target_selected &&
         !tombstone_plan->receipt_persist_allowed &&
         !tombstone_plan->receipt_persisted &&
         !tombstone_plan->receipt_cpu_gpu_sync_allowed &&
         !tombstone_plan->receipt_cpu_gpu_sync_submitted &&
         !tombstone_plan->receipt_error &&
         tombstone_plan->record_required &&
         tombstone_plan->record_allowed &&
         !tombstone_plan->record_submitted &&
         tombstone_plan->record_ticket_selected &&
         tombstone_plan->record_target_selected &&
         !tombstone_plan->record_persist_allowed &&
         !tombstone_plan->record_persisted &&
         !tombstone_plan->record_cpu_gpu_sync_allowed &&
         !tombstone_plan->record_cpu_gpu_sync_submitted &&
         !tombstone_plan->record_error &&
         tombstone_plan->audit_required &&
         tombstone_plan->audit_allowed &&
         !tombstone_plan->audit_submitted &&
         tombstone_plan->audit_ticket_selected &&
         tombstone_plan->audit_target_selected &&
         !tombstone_plan->audit_log_append_allowed &&
         !tombstone_plan->audit_log_appended &&
         !tombstone_plan->audit_cpu_gpu_sync_allowed &&
         !tombstone_plan->audit_cpu_gpu_sync_submitted &&
         tombstone_plan->seal_required &&
         tombstone_plan->seal_allowed &&
         !tombstone_plan->seal_submitted &&
         tombstone_plan->seal_ticket_selected &&
         tombstone_plan->seal_target_selected &&
         !tombstone_plan->seal_state_write_allowed &&
         !tombstone_plan->seal_state_written &&
         !tombstone_plan->seal_cpu_gpu_sync_allowed &&
         !tombstone_plan->seal_cpu_gpu_sync_submitted &&
         tombstone_plan->cleanup_required &&
         tombstone_plan->cleanup_allowed &&
         !tombstone_plan->cleanup_submitted &&
         tombstone_plan->cleanup_ticket_selected &&
         tombstone_plan->cleanup_target_selected &&
         !tombstone_plan->cleanup_resource_release_allowed &&
         !tombstone_plan->cleanup_resource_released &&
         !tombstone_plan->cleanup_cpu_gpu_sync_allowed &&
         !tombstone_plan->cleanup_cpu_gpu_sync_submitted &&
         tombstone_plan->retire_required &&
         tombstone_plan->retire_allowed &&
         !tombstone_plan->retire_submitted &&
         tombstone_plan->retire_ticket_selected &&
         tombstone_plan->retire_target_selected &&
         !tombstone_plan->retire_resource_release_allowed &&
         !tombstone_plan->retire_resource_released &&
         !tombstone_plan->retire_cpu_gpu_sync_allowed &&
         !tombstone_plan->retire_cpu_gpu_sync_submitted &&
         tombstone_plan->ack_required &&
         tombstone_plan->ack_allowed &&
         !tombstone_plan->ack_submitted &&
         tombstone_plan->ack_ticket_selected &&
         tombstone_plan->ack_target_selected &&
         !tombstone_plan->ack_cpu_gpu_sync_allowed &&
         !tombstone_plan->ack_cpu_gpu_sync_submitted &&
         tombstone_plan->completion_required &&
         tombstone_plan->completion_allowed &&
         tombstone_plan->completion_report_required &&
         !tombstone_plan->completion_reported &&
         tombstone_plan->completion_ack_required &&
         !tombstone_plan->completion_acknowledged &&
         tombstone_plan->completion_ticket_selected &&
         tombstone_plan->completion_target_selected &&
         !tombstone_plan->completion_cpu_gpu_sync_allowed &&
         !tombstone_plan->completion_cpu_gpu_sync_submitted &&
         tombstone_plan->deadline_required &&
         tombstone_plan->deadline_allowed &&
         !tombstone_plan->deadline_armed &&
         tombstone_plan->deadline_timer_required &&
         !tombstone_plan->deadline_timer_armed &&
         !tombstone_plan->deadline_expired &&
         tombstone_plan->deadline_completion_required &&
         !tombstone_plan->deadline_completion_reported &&
         !tombstone_plan->deadline_cpu_gpu_sync_allowed &&
         !tombstone_plan->deadline_cpu_gpu_sync_submitted &&
         !tombstone_plan->sync_submitted &&
         !tombstone_plan->sync_wait_allowed &&
         !tombstone_plan->sync_wait_submitted &&
         !tombstone_plan->sync_signal_allowed &&
         !tombstone_plan->sync_signal_submitted &&
         !tombstone_plan->sync_deadline_armed &&
         !tombstone_plan->sync_completion_reported &&
         !tombstone_plan->sync_cpu_gpu_sync_allowed &&
         !tombstone_plan->sync_cpu_gpu_sync_submitted &&
         !tombstone_plan->timeline_submitted &&
         !tombstone_plan->timeline_wait_allowed &&
         !tombstone_plan->timeline_wait_submitted &&
         !tombstone_plan->timeline_signal_allowed &&
         !tombstone_plan->timeline_signal_submitted &&
         !tombstone_plan->timeline_semaphore_allowed &&
         !tombstone_plan->timeline_semaphore_submitted &&
         !tombstone_plan->timeline_value_allocated &&
         !tombstone_plan->timeline_value_published &&
         !tombstone_plan->timeline_cpu_gpu_sync_allowed &&
         !tombstone_plan->timeline_cpu_gpu_sync_submitted &&
         !tombstone_plan->fence_submitted &&
         !tombstone_plan->fence_wait_allowed &&
         !tombstone_plan->fence_wait_submitted &&
         !tombstone_plan->fence_signal_allowed &&
         !tombstone_plan->fence_signal_submitted &&
         !tombstone_plan->fence_fd_export_allowed &&
         !tombstone_plan->fence_fd_exported &&
         !tombstone_plan->fence_cpu_gpu_sync_allowed &&
         !tombstone_plan->fence_cpu_gpu_sync_submitted &&
         !tombstone_plan->barrier_submitted &&
         !tombstone_plan->barrier_memory_visibility_established &&
         !tombstone_plan->barrier_cache_visibility_established &&
         !tombstone_plan->barrier_cpu_gpu_sync_allowed &&
         !tombstone_plan->barrier_cpu_gpu_sync_submitted &&
         !tombstone_plan->flush_submitted &&
         !tombstone_plan->flush_cache_clean_allowed &&
         !tombstone_plan->flush_cache_cleaned &&
         !tombstone_plan->flush_memory_barrier_allowed &&
         !tombstone_plan->flush_memory_barrier_submitted &&
         !tombstone_plan->framebuffer_submitted &&
         !tombstone_plan->framebuffer_mapped &&
         !tombstone_plan->framebuffer_write_allowed &&
         !tombstone_plan->framebuffer_written &&
         !tombstone_plan->framebuffer_flushed &&
         !tombstone_plan->framebuffer_cache_cleaned &&
         !tombstone_plan->blit_submitted &&
         !tombstone_plan->blit_source_buffer_mapped &&
         !tombstone_plan->blit_destination_buffer_mapped &&
         !tombstone_plan->blit_pixels_copied &&
         !tombstone_plan->blit_dma_allowed &&
         !tombstone_plan->blit_dma_submitted &&
         !tombstone_plan->output_submitted &&
         !tombstone_plan->output_buffer_attached &&
         !tombstone_plan->output_buffer_submitted &&
         !tombstone_plan->output_flip_allowed &&
         !tombstone_plan->output_flip_submitted &&
         !tombstone_plan->display_submitted &&
         !tombstone_plan->display_buffer_attached &&
         !tombstone_plan->display_buffer_submitted &&
         !tombstone_plan->display_mode_committed &&
         !tombstone_plan->display_flip_allowed &&
         !tombstone_plan->display_flip_submitted &&
         !tombstone_plan->scanout_submitted &&
         !tombstone_plan->scanout_buffer_attached &&
         !tombstone_plan->scanout_buffer_submitted &&
         !tombstone_plan->vsync_submitted &&
         !tombstone_plan->vsync_wait_submitted &&
         !tombstone_plan->vsync_fence_armed &&
         !tombstone_plan->schedule_submitted &&
         !tombstone_plan->present_submitted &&
         !tombstone_plan->damage_submitted &&
         !tombstone_plan->compositor_damage_submitted &&
         !tombstone_plan->frame_timer_armed &&
         !tombstone_plan->compositor_wake_allowed &&
         !tombstone_plan->compositor_wake_submitted &&
         !tombstone_plan->page_flip_allowed &&
         !tombstone_plan->page_flip_submitted &&
         tombstone_plan->route_selected &&
         !tombstone_plan->route_blocked &&
         tombstone_plan->credential_session_safe &&
         tombstone_plan->credential_storage_wiped &&
         tombstone_plan->credential_redacted &&
         tombstone_plan->length_redacted &&
         !tombstone_plan->raw_secret_exposed &&
         !tombstone_plan->masked_text_exposed &&
         tombstone_plan->submit_blocked &&
         !tombstone_plan->submit_enabled &&
         !tombstone_plan->auth_attempt_allowed &&
         !tombstone_plan->submit_callback_bound &&
         !tombstone_plan->auth_callback_bound &&
         tombstone_plan->text_login_authoritative;
}

static void login_window_credential_screen_compaction_plan_copy_safe_fields(
    const struct login_window_credential_screen_tombstone_plan *tombstone_plan,
    struct login_window_credential_screen_compaction_plan *out) {
  out->tombstone_required = tombstone_plan->tombstone_required ? 1 : 0;
  out->tombstone_allowed = tombstone_plan->tombstone_allowed ? 1 : 0;
  out->tombstone_ticket_selected =
      tombstone_plan->tombstone_ticket_selected ? 1 : 0;
  out->tombstone_target_selected =
      tombstone_plan->tombstone_target_selected ? 1 : 0;
  out->recovery_text_session_required =
      tombstone_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = tombstone_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      tombstone_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = tombstone_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = tombstone_plan->view;
  out->widget_tree = tombstone_plan->widget_tree;
  out->tombstone_ticket = tombstone_plan->tombstone_ticket;
  out->focus_target = tombstone_plan->focus_target;
  out->primary_action = tombstone_plan->primary_action;
  out->route = tombstone_plan->route;
  out->tombstone_policy = tombstone_plan->tombstone_policy;
}

int login_window_credential_screen_compaction_plan_build(
    const struct login_window_credential_screen_tombstone_plan *tombstone_plan,
    struct login_window_credential_screen_compaction_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_compaction_plan_reset(out, tombstone_plan ? 1 : 0);
  if (!tombstone_plan) return 0;
  out->requested_action = tombstone_plan->requested_action;
  out->tombstone_plan_safe = tombstone_plan->tombstone_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_compaction_plan_tombstone_is_safe(tombstone_plan)) {
    out->event_type = "credential-screen-compaction-plan-unsafe";
    out->blocked_reason = "credential-compaction-plan-unsafe";
    out->message = "Credential screen compaction plan unsafe; use text login.";
    return 0;
  }
  out->compaction_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = tombstone_plan->action_allowed ? 1 : 0;
  out->action_blocked = tombstone_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = tombstone_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_compaction_plan_copy_safe_fields(tombstone_plan, out);
  out->compaction_allowed = 1;
  out->compaction_ticket_selected = 1;
  out->compaction_target_selected = 1;
  out->compaction_error = 0;
  out->compaction_policy = "declarative-compaction-no-write";
  out->event_type = "credential-screen-compaction-plan-ready";
  out->state = "compaction-ready";
  out->message = "Credential screen compaction ticket ready; no storage compacted.";
  out->blocked_reason = tombstone_plan->blocked_reason;
  if (tombstone_plan->submit_requested) {
    out->compaction_ticket = "text-login-fallback-compaction-ticket";
    out->compositor_target = "text-login-fallback-compaction";
    out->compaction_policy = "fallback-compaction-declarative";
    out->compaction_text_login = 1;
    out->compaction_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "compaction-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (tombstone_plan->tombstone_credential_panel &&
      tombstone_plan->tombstone_credential_input &&
      tombstone_plan->tombstone_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->compaction_ticket = "credential-screen-compaction-ticket";
    out->compositor_target = "credential-screen-compaction";
    out->compaction_credential_panel = 1;
    out->compaction_credential_input = 1;
    out->compaction_credential_focus = 1;
    out->compaction_text_login = 0;
    out->compaction_text_login_fallback = 0;
    out->state = "compaction-credential-ready";
    out->message = "Credential compaction ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (tombstone_plan->tombstone_text_recovery && out->recovery_text_session_required) {
    out->compaction_ticket = "text-recovery-compaction-ticket";
    out->compositor_target = "text-recovery-compaction";
    out->compaction_text_recovery = 1;
    out->compaction_text_login = 1;
    out->compaction_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "compaction-text-recovery-ready";
    out->message = "Text recovery compaction ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (tombstone_plan->tombstone_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->compaction_ticket = "text-login-resume-compaction-ticket";
    out->compositor_target = "text-login-resume-compaction";
    out->compaction_policy = "full-compaction-declarative";
    out->compaction_text_login = 1;
    out->compaction_text_login_resume = 1;
    out->compaction_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "compaction-resume-ready";
    out->message = "Text login resume compaction ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->compaction_ticket = "text-login-fallback-compaction-ticket";
  out->compositor_target = "text-login-fallback-compaction";
  out->compaction_policy = "fallback-compaction-declarative";
  out->compaction_text_login = 1;
  out->compaction_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "compaction-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
