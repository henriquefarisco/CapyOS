/*
 * src/auth/login_runtime/cleanup_plan.c
 *
 * Credential-screen cleanup plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.32 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the cleanup stage of the credential pipeline:
 *
 *   - login_window_credential_screen_cleanup_plan_reset (static)
 *   - login_window_credential_screen_cleanup_plan_retire_is_safe (static)
 *   - login_window_credential_screen_cleanup_plan_build
 *
 * The cleanup-plan converts a fail-closed retire-plan into a
 * cleanup contract for the downstream seal stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_retire_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_cleanup_plan_reset(
    struct login_window_credential_screen_cleanup_plan *out,
    int retire_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_CLEANUP_PLAN_VERSION;
  out->retire_plan_available = retire_plan_available ? 1 : 0;
  out->retire_plan_safe = 0;
  out->cleanup_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
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
  out->cleanup_required = 1;
  out->cleanup_allowed = 0;
  out->cleanup_submitted = 0;
  out->cleanup_ticket_selected = 0;
  out->cleanup_target_selected = 0;
  out->cleanup_resource_release_allowed = 0;
  out->cleanup_resource_released = 0;
  out->cleanup_cpu_gpu_sync_allowed = 0;
  out->cleanup_cpu_gpu_sync_submitted = 0;
  out->cleanup_credential_panel = 0;
  out->cleanup_credential_input = 0;
  out->cleanup_credential_focus = 0;
  out->cleanup_text_recovery = 0;
  out->cleanup_text_login = 1;
  out->cleanup_text_login_resume = 0;
  out->cleanup_text_login_fallback = 1;
  out->cleanup_error = 1;
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
  out->retire_ticket = "text-login-fallback-retire-ticket";
  out->cleanup_ticket = "text-login-fallback-cleanup-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->retire_policy = "retire-disabled";
  out->cleanup_policy = "cleanup-disabled";
  out->event_type = "credential-screen-cleanup-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "retire-plan-unavailable";
}

static int login_window_credential_screen_cleanup_plan_retire_is_safe(
    const struct login_window_credential_screen_retire_plan *retire_plan) {
  return retire_plan->retire_plan_safe && retire_plan->retire_required &&
         retire_plan->retire_allowed && !retire_plan->retire_submitted &&
         retire_plan->retire_ticket_selected && retire_plan->retire_target_selected &&
         !retire_plan->retire_resource_release_allowed &&
         !retire_plan->retire_resource_released && !retire_plan->retire_cpu_gpu_sync_allowed &&
         !retire_plan->retire_cpu_gpu_sync_submitted && !retire_plan->retire_error &&
         retire_plan->ack_required && retire_plan->ack_allowed && !retire_plan->ack_submitted &&
         retire_plan->ack_ticket_selected && retire_plan->ack_target_selected &&
         !retire_plan->ack_cpu_gpu_sync_allowed && !retire_plan->ack_cpu_gpu_sync_submitted &&
         retire_plan->completion_required && retire_plan->completion_allowed &&
         retire_plan->completion_report_required && !retire_plan->completion_reported &&
         retire_plan->completion_ack_required && !retire_plan->completion_acknowledged &&
         retire_plan->completion_ticket_selected && retire_plan->completion_target_selected &&
         !retire_plan->completion_cpu_gpu_sync_allowed &&
         !retire_plan->completion_cpu_gpu_sync_submitted && retire_plan->deadline_required &&
         retire_plan->deadline_allowed && !retire_plan->deadline_armed &&
         retire_plan->deadline_timer_required && !retire_plan->deadline_timer_armed &&
         !retire_plan->deadline_expired && retire_plan->deadline_completion_required &&
         !retire_plan->deadline_completion_reported &&
         !retire_plan->deadline_cpu_gpu_sync_allowed &&
         !retire_plan->deadline_cpu_gpu_sync_submitted && !retire_plan->sync_submitted &&
         !retire_plan->sync_wait_allowed && !retire_plan->sync_wait_submitted &&
         !retire_plan->sync_signal_allowed && !retire_plan->sync_signal_submitted &&
         !retire_plan->sync_deadline_armed && !retire_plan->sync_completion_reported &&
         !retire_plan->sync_cpu_gpu_sync_allowed && !retire_plan->sync_cpu_gpu_sync_submitted &&
         !retire_plan->timeline_submitted && !retire_plan->timeline_wait_allowed &&
         !retire_plan->timeline_wait_submitted && !retire_plan->timeline_signal_allowed &&
         !retire_plan->timeline_signal_submitted && !retire_plan->timeline_semaphore_allowed &&
         !retire_plan->timeline_semaphore_submitted && !retire_plan->timeline_value_allocated &&
         !retire_plan->timeline_value_published && !retire_plan->timeline_cpu_gpu_sync_allowed &&
         !retire_plan->timeline_cpu_gpu_sync_submitted && !retire_plan->fence_submitted &&
         !retire_plan->fence_wait_allowed && !retire_plan->fence_wait_submitted &&
         !retire_plan->fence_signal_allowed && !retire_plan->fence_signal_submitted &&
         !retire_plan->fence_fd_export_allowed && !retire_plan->fence_fd_exported &&
         !retire_plan->fence_cpu_gpu_sync_allowed && !retire_plan->fence_cpu_gpu_sync_submitted &&
         !retire_plan->barrier_submitted && !retire_plan->barrier_memory_visibility_established &&
         !retire_plan->barrier_cache_visibility_established &&
         !retire_plan->barrier_cpu_gpu_sync_allowed && !retire_plan->barrier_cpu_gpu_sync_submitted &&
         !retire_plan->flush_submitted && !retire_plan->flush_cache_clean_allowed &&
         !retire_plan->flush_cache_cleaned && !retire_plan->flush_memory_barrier_allowed &&
         !retire_plan->flush_memory_barrier_submitted && !retire_plan->framebuffer_submitted &&
         !retire_plan->framebuffer_mapped && !retire_plan->framebuffer_write_allowed &&
         !retire_plan->framebuffer_written && !retire_plan->framebuffer_flushed &&
         !retire_plan->framebuffer_cache_cleaned && !retire_plan->blit_submitted &&
         !retire_plan->blit_source_buffer_mapped && !retire_plan->blit_destination_buffer_mapped &&
         !retire_plan->blit_pixels_copied && !retire_plan->blit_dma_allowed &&
         !retire_plan->blit_dma_submitted && !retire_plan->output_submitted &&
         !retire_plan->output_buffer_attached && !retire_plan->output_buffer_submitted &&
         !retire_plan->output_flip_allowed && !retire_plan->output_flip_submitted &&
         !retire_plan->display_submitted && !retire_plan->display_buffer_attached &&
         !retire_plan->display_buffer_submitted && !retire_plan->display_mode_committed &&
         !retire_plan->display_flip_allowed && !retire_plan->display_flip_submitted &&
         !retire_plan->scanout_submitted && !retire_plan->scanout_buffer_attached &&
         !retire_plan->scanout_buffer_submitted && !retire_plan->vsync_submitted &&
         !retire_plan->vsync_wait_submitted && !retire_plan->vsync_fence_armed &&
         !retire_plan->schedule_submitted && !retire_plan->present_submitted &&
         !retire_plan->damage_submitted && !retire_plan->compositor_damage_submitted &&
         !retire_plan->frame_timer_armed && !retire_plan->compositor_wake_allowed &&
         !retire_plan->compositor_wake_submitted && !retire_plan->page_flip_allowed &&
         !retire_plan->page_flip_submitted && retire_plan->route_selected &&
         !retire_plan->route_blocked && retire_plan->credential_session_safe &&
         retire_plan->credential_storage_wiped && retire_plan->credential_redacted &&
         retire_plan->length_redacted && !retire_plan->raw_secret_exposed &&
         !retire_plan->masked_text_exposed && retire_plan->submit_blocked &&
         !retire_plan->submit_enabled && !retire_plan->auth_attempt_allowed &&
         !retire_plan->submit_callback_bound && !retire_plan->auth_callback_bound &&
         retire_plan->text_login_authoritative;
}

int login_window_credential_screen_cleanup_plan_build(
    const struct login_window_credential_screen_retire_plan *retire_plan,
    struct login_window_credential_screen_cleanup_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_cleanup_plan_reset(out, retire_plan ? 1 : 0);
  if (!retire_plan) return 0;
  out->requested_action = retire_plan->requested_action;
  out->retire_plan_safe = retire_plan->retire_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_cleanup_plan_retire_is_safe(retire_plan)) {
    out->event_type = "credential-screen-cleanup-plan-unsafe";
    out->blocked_reason = "credential-cleanup-plan-unsafe";
    out->message = "Credential screen cleanup plan unsafe; use text login.";
    return 0;
  }
  out->cleanup_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = retire_plan->action_allowed ? 1 : 0;
  out->action_blocked = retire_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = retire_plan->input_focus_allowed ? 1 : 0;
  out->retire_required = retire_plan->retire_required ? 1 : 0;
  out->retire_allowed = retire_plan->retire_allowed ? 1 : 0;
  out->retire_ticket_selected = retire_plan->retire_ticket_selected ? 1 : 0;
  out->retire_target_selected = retire_plan->retire_target_selected ? 1 : 0;
  out->ack_required = retire_plan->ack_required ? 1 : 0;
  out->ack_allowed = retire_plan->ack_allowed ? 1 : 0;
  out->ack_ticket_selected = retire_plan->ack_ticket_selected ? 1 : 0;
  out->ack_target_selected = retire_plan->ack_target_selected ? 1 : 0;
  out->completion_required = retire_plan->completion_required ? 1 : 0;
  out->completion_allowed = retire_plan->completion_allowed ? 1 : 0;
  out->completion_report_required = retire_plan->completion_report_required ? 1 : 0;
  out->completion_ack_required = retire_plan->completion_ack_required ? 1 : 0;
  out->completion_ticket_selected = retire_plan->completion_ticket_selected ? 1 : 0;
  out->completion_target_selected = retire_plan->completion_target_selected ? 1 : 0;
  out->deadline_required = retire_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = retire_plan->deadline_allowed ? 1 : 0;
  out->cleanup_allowed = 1;
  out->cleanup_ticket_selected = 1;
  out->cleanup_target_selected = 1;
  out->cleanup_error = 0;
  out->recovery_text_session_required = retire_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = retire_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = retire_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = retire_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = retire_plan->view;
  out->widget_tree = retire_plan->widget_tree;
  out->retire_ticket = retire_plan->retire_ticket;
  out->focus_target = retire_plan->focus_target;
  out->primary_action = retire_plan->primary_action;
  out->route = retire_plan->route;
  out->compositor_target = retire_plan->compositor_target;
  out->retire_policy = retire_plan->retire_policy;
  out->cleanup_policy = "declarative-cleanup-no-release";
  out->event_type = "credential-screen-cleanup-plan-ready";
  out->state = "cleanup-ready";
  out->message = "Credential screen cleanup ticket ready; no cleanup submitted.";
  out->blocked_reason = retire_plan->blocked_reason;
  if (retire_plan->submit_requested) {
    out->cleanup_ticket = "text-login-fallback-cleanup-ticket";
    out->compositor_target = "text-login-fallback-cleanup";
    out->cleanup_policy = "fallback-cleanup-declarative";
    out->cleanup_text_login = 1;
    out->cleanup_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "cleanup-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (retire_plan->retire_credential_panel && retire_plan->retire_credential_input &&
      retire_plan->retire_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->cleanup_ticket = "credential-screen-cleanup-ticket";
    out->compositor_target = "credential-screen-cleanup";
    out->cleanup_credential_panel = 1;
    out->cleanup_credential_input = 1;
    out->cleanup_credential_focus = 1;
    out->cleanup_text_login = 0;
    out->cleanup_text_login_fallback = 0;
    out->state = "cleanup-credential-ready";
    out->message = "Credential cleanup ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (retire_plan->retire_text_recovery && out->recovery_text_session_required) {
    out->cleanup_ticket = "text-recovery-cleanup-ticket";
    out->compositor_target = "text-recovery-cleanup";
    out->cleanup_text_recovery = 1;
    out->cleanup_text_login = 1;
    out->cleanup_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "cleanup-text-recovery-ready";
    out->message = "Text recovery cleanup ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (retire_plan->retire_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->cleanup_ticket = "text-login-resume-cleanup-ticket";
    out->compositor_target = "text-login-resume-cleanup";
    out->cleanup_policy = "full-cleanup-declarative";
    out->cleanup_text_login = 1;
    out->cleanup_text_login_resume = 1;
    out->cleanup_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "cleanup-resume-ready";
    out->message = "Text login resume cleanup ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->cleanup_ticket = "text-login-fallback-cleanup-ticket";
  out->compositor_target = "text-login-fallback-cleanup";
  out->cleanup_policy = "fallback-cleanup-declarative";
  out->cleanup_text_login = 1;
  out->cleanup_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "cleanup-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
