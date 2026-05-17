/*
 * src/auth/login_runtime/sync_plan.c
 *
 * Credential-screen sync plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.27 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the sync stage of the credential pipeline:
 *
 *   - login_window_credential_screen_sync_plan_reset (static)
 *   - login_window_credential_screen_sync_plan_timeline_is_safe (static)
 *   - login_window_credential_screen_sync_plan_build
 *
 * The sync-plan converts a fail-closed timeline-plan into a sync
 * contract for the downstream deadline stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_timeline_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_sync_plan_reset(
    struct login_window_credential_screen_sync_plan *out,
    int timeline_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SYNC_PLAN_VERSION;
  out->timeline_plan_available = timeline_plan_available ? 1 : 0;
  out->timeline_plan_safe = 0;
  out->sync_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->timeline_allowed = 0;
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
  out->sync_required = 1;
  out->sync_allowed = 0;
  out->sync_submitted = 0;
  out->sync_ticket_selected = 0;
  out->sync_target_selected = 0;
  out->sync_wait_required = 1;
  out->sync_wait_allowed = 0;
  out->sync_wait_submitted = 0;
  out->sync_signal_allowed = 0;
  out->sync_signal_submitted = 0;
  out->sync_deadline_required = 1;
  out->sync_deadline_armed = 0;
  out->sync_completion_required = 1;
  out->sync_completion_reported = 0;
  out->sync_cpu_gpu_sync_allowed = 0;
  out->sync_cpu_gpu_sync_submitted = 0;
  out->sync_credential_panel = 0;
  out->sync_credential_input = 0;
  out->sync_credential_focus = 0;
  out->sync_text_recovery = 0;
  out->sync_text_login = 1;
  out->sync_text_login_resume = 0;
  out->sync_text_login_fallback = 1;
  out->sync_error = 1;
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
  out->timeline_ticket = "text-login-fallback-timeline-ticket";
  out->sync_ticket = "text-login-fallback-sync-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->timeline_policy = "timeline-disabled";
  out->sync_policy = "sync-disabled";
  out->event_type = "credential-screen-sync-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "timeline-plan-unavailable";
}

static int login_window_credential_screen_sync_plan_timeline_is_safe(
    const struct login_window_credential_screen_timeline_plan *timeline_plan) {
  return timeline_plan->timeline_plan_safe && timeline_plan->timeline_required &&
         timeline_plan->timeline_allowed && !timeline_plan->timeline_submitted &&
         timeline_plan->timeline_ticket_selected && timeline_plan->timeline_target_selected &&
         timeline_plan->timeline_wait_required && !timeline_plan->timeline_wait_allowed &&
         !timeline_plan->timeline_wait_submitted && !timeline_plan->timeline_signal_allowed &&
         !timeline_plan->timeline_signal_submitted && !timeline_plan->timeline_semaphore_allowed &&
         !timeline_plan->timeline_semaphore_submitted && timeline_plan->timeline_value_required &&
         !timeline_plan->timeline_value_allocated && !timeline_plan->timeline_value_published &&
         !timeline_plan->timeline_cpu_gpu_sync_allowed &&
         !timeline_plan->timeline_cpu_gpu_sync_submitted && !timeline_plan->timeline_error &&
         timeline_plan->fence_allowed && !timeline_plan->fence_submitted &&
         !timeline_plan->fence_wait_allowed && !timeline_plan->fence_wait_submitted &&
         !timeline_plan->fence_signal_allowed && !timeline_plan->fence_signal_submitted &&
         !timeline_plan->fence_fd_export_allowed && !timeline_plan->fence_fd_exported &&
         !timeline_plan->fence_cpu_gpu_sync_allowed &&
         !timeline_plan->fence_cpu_gpu_sync_submitted && !timeline_plan->barrier_submitted &&
         !timeline_plan->barrier_memory_visibility_established &&
         !timeline_plan->barrier_cache_visibility_established &&
         !timeline_plan->barrier_cpu_gpu_sync_allowed &&
         !timeline_plan->barrier_cpu_gpu_sync_submitted &&
         !timeline_plan->flush_submitted && !timeline_plan->flush_cache_clean_allowed &&
         !timeline_plan->flush_cache_cleaned && !timeline_plan->flush_memory_barrier_allowed &&
         !timeline_plan->flush_memory_barrier_submitted && !timeline_plan->framebuffer_submitted &&
         !timeline_plan->framebuffer_mapped && !timeline_plan->framebuffer_write_allowed &&
         !timeline_plan->framebuffer_written && !timeline_plan->framebuffer_flushed &&
         !timeline_plan->framebuffer_cache_cleaned && !timeline_plan->blit_submitted &&
         !timeline_plan->blit_source_buffer_mapped && !timeline_plan->blit_destination_buffer_mapped &&
         !timeline_plan->blit_pixels_copied && !timeline_plan->blit_dma_allowed &&
         !timeline_plan->blit_dma_submitted && !timeline_plan->output_submitted &&
         !timeline_plan->output_buffer_attached && !timeline_plan->output_buffer_submitted &&
         !timeline_plan->output_flip_allowed && !timeline_plan->output_flip_submitted &&
         !timeline_plan->display_submitted && !timeline_plan->display_buffer_attached &&
         !timeline_plan->display_buffer_submitted && !timeline_plan->display_mode_committed &&
         !timeline_plan->display_flip_allowed && !timeline_plan->display_flip_submitted &&
         !timeline_plan->scanout_submitted && !timeline_plan->scanout_buffer_attached &&
         !timeline_plan->scanout_buffer_submitted && !timeline_plan->vsync_submitted &&
         !timeline_plan->vsync_wait_submitted && !timeline_plan->vsync_fence_armed &&
         !timeline_plan->schedule_submitted && !timeline_plan->present_submitted &&
         !timeline_plan->damage_submitted && !timeline_plan->compositor_damage_submitted &&
         !timeline_plan->frame_timer_armed && !timeline_plan->compositor_wake_allowed &&
         !timeline_plan->compositor_wake_submitted && !timeline_plan->page_flip_allowed &&
         !timeline_plan->page_flip_submitted && timeline_plan->route_selected &&
         !timeline_plan->route_blocked && timeline_plan->credential_session_safe &&
         timeline_plan->credential_storage_wiped && timeline_plan->credential_redacted &&
         timeline_plan->length_redacted && !timeline_plan->raw_secret_exposed &&
         !timeline_plan->masked_text_exposed && timeline_plan->submit_blocked &&
         !timeline_plan->submit_enabled && !timeline_plan->auth_attempt_allowed &&
         !timeline_plan->submit_callback_bound && !timeline_plan->auth_callback_bound &&
         timeline_plan->text_login_authoritative;
}

int login_window_credential_screen_sync_plan_build(
    const struct login_window_credential_screen_timeline_plan *timeline_plan,
    struct login_window_credential_screen_sync_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_sync_plan_reset(out, timeline_plan ? 1 : 0);
  if (!timeline_plan) return 0;
  out->requested_action = timeline_plan->requested_action;
  out->timeline_plan_safe = timeline_plan->timeline_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_sync_plan_timeline_is_safe(timeline_plan)) {
    out->event_type = "credential-screen-sync-plan-unsafe";
    out->blocked_reason = "credential-sync-plan-unsafe";
    out->message = "Credential screen sync plan unsafe; use text login.";
    return 0;
  }
  out->sync_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = timeline_plan->action_allowed ? 1 : 0;
  out->action_blocked = timeline_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = timeline_plan->input_focus_allowed ? 1 : 0;
  out->timeline_allowed = 1;
  out->sync_allowed = 1;
  out->sync_ticket_selected = 1;
  out->sync_target_selected = 1;
  out->sync_error = 0;
  out->recovery_text_session_required = timeline_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = timeline_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = timeline_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = timeline_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = timeline_plan->view;
  out->widget_tree = timeline_plan->widget_tree;
  out->timeline_ticket = timeline_plan->timeline_ticket;
  out->focus_target = timeline_plan->focus_target;
  out->primary_action = timeline_plan->primary_action;
  out->route = timeline_plan->route;
  out->compositor_target = timeline_plan->compositor_target;
  out->timeline_policy = timeline_plan->timeline_policy;
  out->sync_policy = "declarative-sync-no-submit";
  out->event_type = "credential-screen-sync-plan-ready";
  out->state = "sync-ready";
  out->message = "Credential screen sync ticket ready; no sync submitted.";
  out->blocked_reason = timeline_plan->blocked_reason;
  if (timeline_plan->submit_requested) {
    out->sync_ticket = "text-login-fallback-sync-ticket";
    out->compositor_target = "text-login-fallback-sync";
    out->sync_policy = "fallback-sync-declarative";
    out->sync_text_login = 1;
    out->sync_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "sync-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (timeline_plan->timeline_credential_panel && timeline_plan->timeline_credential_input &&
      timeline_plan->timeline_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->sync_ticket = "credential-screen-sync-ticket";
    out->compositor_target = "credential-screen-sync";
    out->sync_credential_panel = 1;
    out->sync_credential_input = 1;
    out->sync_credential_focus = 1;
    out->sync_text_login = 0;
    out->sync_text_login_fallback = 0;
    out->state = "sync-credential-ready";
    out->message = "Credential sync ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (timeline_plan->timeline_text_recovery && out->recovery_text_session_required) {
    out->sync_ticket = "text-recovery-sync-ticket";
    out->compositor_target = "text-recovery-sync";
    out->sync_text_recovery = 1;
    out->sync_text_login = 1;
    out->sync_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "sync-text-recovery-ready";
    out->message = "Text recovery sync ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (timeline_plan->timeline_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->sync_ticket = "text-login-resume-sync-ticket";
    out->compositor_target = "text-login-resume-sync";
    out->sync_policy = "full-sync-declarative";
    out->sync_text_login = 1;
    out->sync_text_login_resume = 1;
    out->sync_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "sync-resume-ready";
    out->message = "Text login resume sync ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->sync_ticket = "text-login-fallback-sync-ticket";
  out->compositor_target = "text-login-fallback-sync";
  out->sync_policy = "fallback-sync-declarative";
  out->sync_text_login = 1;
  out->sync_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "sync-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
