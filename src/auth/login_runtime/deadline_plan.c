/*
 * src/auth/login_runtime/deadline_plan.c
 *
 * Credential-screen deadline plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.28 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the deadline stage of the credential pipeline:
 *
 *   - login_window_credential_screen_deadline_plan_reset (static)
 *   - login_window_credential_screen_deadline_plan_sync_is_safe (static)
 *   - login_window_credential_screen_deadline_plan_build
 *
 * The deadline-plan converts a fail-closed sync-plan into a
 * deadline contract for the downstream completion stage.  The
 * static `_reset` helper is the canonical "blocked" initializer and
 * the static `_sync_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_deadline_plan_reset(
    struct login_window_credential_screen_deadline_plan *out,
    int sync_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_DEADLINE_PLAN_VERSION;
  out->sync_plan_available = sync_plan_available ? 1 : 0;
  out->sync_plan_safe = 0;
  out->deadline_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->sync_allowed = 0;
  out->sync_submitted = 0;
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
  out->deadline_required = 1;
  out->deadline_allowed = 0;
  out->deadline_armed = 0;
  out->deadline_ticket_selected = 0;
  out->deadline_target_selected = 0;
  out->deadline_timer_required = 1;
  out->deadline_timer_armed = 0;
  out->deadline_expired = 0;
  out->deadline_completion_required = 1;
  out->deadline_completion_reported = 0;
  out->deadline_cpu_gpu_sync_allowed = 0;
  out->deadline_cpu_gpu_sync_submitted = 0;
  out->deadline_credential_panel = 0;
  out->deadline_credential_input = 0;
  out->deadline_credential_focus = 0;
  out->deadline_text_recovery = 0;
  out->deadline_text_login = 1;
  out->deadline_text_login_resume = 0;
  out->deadline_text_login_fallback = 1;
  out->deadline_error = 1;
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
  out->sync_ticket = "text-login-fallback-sync-ticket";
  out->deadline_ticket = "text-login-fallback-deadline-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->sync_policy = "sync-disabled";
  out->deadline_policy = "deadline-disabled";
  out->event_type = "credential-screen-deadline-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "sync-plan-unavailable";
}

static int login_window_credential_screen_deadline_plan_sync_is_safe(
    const struct login_window_credential_screen_sync_plan *sync_plan) {
  return sync_plan->sync_plan_safe && sync_plan->sync_required &&
         sync_plan->sync_allowed && !sync_plan->sync_submitted &&
         sync_plan->sync_ticket_selected && sync_plan->sync_target_selected &&
         sync_plan->sync_wait_required && !sync_plan->sync_wait_allowed &&
         !sync_plan->sync_wait_submitted && !sync_plan->sync_signal_allowed &&
         !sync_plan->sync_signal_submitted && sync_plan->sync_deadline_required &&
         !sync_plan->sync_deadline_armed && sync_plan->sync_completion_required &&
         !sync_plan->sync_completion_reported && !sync_plan->sync_cpu_gpu_sync_allowed &&
         !sync_plan->sync_cpu_gpu_sync_submitted && !sync_plan->sync_error &&
         sync_plan->timeline_allowed && !sync_plan->timeline_submitted &&
         !sync_plan->timeline_wait_allowed && !sync_plan->timeline_wait_submitted &&
         !sync_plan->timeline_signal_allowed && !sync_plan->timeline_signal_submitted &&
         !sync_plan->timeline_semaphore_allowed && !sync_plan->timeline_semaphore_submitted &&
         !sync_plan->timeline_value_allocated && !sync_plan->timeline_value_published &&
         !sync_plan->timeline_cpu_gpu_sync_allowed &&
         !sync_plan->timeline_cpu_gpu_sync_submitted && !sync_plan->fence_submitted &&
         !sync_plan->fence_wait_allowed && !sync_plan->fence_wait_submitted &&
         !sync_plan->fence_signal_allowed && !sync_plan->fence_signal_submitted &&
         !sync_plan->fence_fd_export_allowed && !sync_plan->fence_fd_exported &&
         !sync_plan->fence_cpu_gpu_sync_allowed && !sync_plan->fence_cpu_gpu_sync_submitted &&
         !sync_plan->barrier_submitted && !sync_plan->barrier_memory_visibility_established &&
         !sync_plan->barrier_cache_visibility_established && !sync_plan->barrier_cpu_gpu_sync_allowed &&
         !sync_plan->barrier_cpu_gpu_sync_submitted && !sync_plan->flush_submitted &&
         !sync_plan->flush_cache_clean_allowed && !sync_plan->flush_cache_cleaned &&
         !sync_plan->flush_memory_barrier_allowed && !sync_plan->flush_memory_barrier_submitted &&
         !sync_plan->framebuffer_submitted && !sync_plan->framebuffer_mapped &&
         !sync_plan->framebuffer_write_allowed && !sync_plan->framebuffer_written &&
         !sync_plan->framebuffer_flushed && !sync_plan->framebuffer_cache_cleaned &&
         !sync_plan->blit_submitted && !sync_plan->blit_source_buffer_mapped &&
         !sync_plan->blit_destination_buffer_mapped && !sync_plan->blit_pixels_copied &&
         !sync_plan->blit_dma_allowed && !sync_plan->blit_dma_submitted &&
         !sync_plan->output_submitted && !sync_plan->output_buffer_attached &&
         !sync_plan->output_buffer_submitted && !sync_plan->output_flip_allowed &&
         !sync_plan->output_flip_submitted && !sync_plan->display_submitted &&
         !sync_plan->display_buffer_attached && !sync_plan->display_buffer_submitted &&
         !sync_plan->display_mode_committed && !sync_plan->display_flip_allowed &&
         !sync_plan->display_flip_submitted && !sync_plan->scanout_submitted &&
         !sync_plan->scanout_buffer_attached && !sync_plan->scanout_buffer_submitted &&
         !sync_plan->vsync_submitted && !sync_plan->vsync_wait_submitted &&
         !sync_plan->vsync_fence_armed && !sync_plan->schedule_submitted &&
         !sync_plan->present_submitted && !sync_plan->damage_submitted &&
         !sync_plan->compositor_damage_submitted && !sync_plan->frame_timer_armed &&
         !sync_plan->compositor_wake_allowed && !sync_plan->compositor_wake_submitted &&
         !sync_plan->page_flip_allowed && !sync_plan->page_flip_submitted &&
         sync_plan->route_selected && !sync_plan->route_blocked &&
         sync_plan->credential_session_safe && sync_plan->credential_storage_wiped &&
         sync_plan->credential_redacted && sync_plan->length_redacted &&
         !sync_plan->raw_secret_exposed && !sync_plan->masked_text_exposed &&
         sync_plan->submit_blocked && !sync_plan->submit_enabled &&
         !sync_plan->auth_attempt_allowed && !sync_plan->submit_callback_bound &&
         !sync_plan->auth_callback_bound && sync_plan->text_login_authoritative;
}

int login_window_credential_screen_deadline_plan_build(
    const struct login_window_credential_screen_sync_plan *sync_plan,
    struct login_window_credential_screen_deadline_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_deadline_plan_reset(out, sync_plan ? 1 : 0);
  if (!sync_plan) return 0;
  out->requested_action = sync_plan->requested_action;
  out->sync_plan_safe = sync_plan->sync_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_deadline_plan_sync_is_safe(sync_plan)) {
    out->event_type = "credential-screen-deadline-plan-unsafe";
    out->blocked_reason = "credential-deadline-plan-unsafe";
    out->message = "Credential screen deadline plan unsafe; use text login.";
    return 0;
  }
  out->deadline_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = sync_plan->action_allowed ? 1 : 0;
  out->action_blocked = sync_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = sync_plan->input_focus_allowed ? 1 : 0;
  out->sync_allowed = 1;
  out->deadline_allowed = 1;
  out->deadline_ticket_selected = 1;
  out->deadline_target_selected = 1;
  out->deadline_error = 0;
  out->recovery_text_session_required = sync_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = sync_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = sync_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = sync_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = sync_plan->view;
  out->widget_tree = sync_plan->widget_tree;
  out->sync_ticket = sync_plan->sync_ticket;
  out->focus_target = sync_plan->focus_target;
  out->primary_action = sync_plan->primary_action;
  out->route = sync_plan->route;
  out->compositor_target = sync_plan->compositor_target;
  out->sync_policy = sync_plan->sync_policy;
  out->deadline_policy = "declarative-deadline-no-arm";
  out->event_type = "credential-screen-deadline-plan-ready";
  out->state = "deadline-ready";
  out->message = "Credential screen deadline ticket ready; no deadline armed.";
  out->blocked_reason = sync_plan->blocked_reason;
  if (sync_plan->submit_requested) {
    out->deadline_ticket = "text-login-fallback-deadline-ticket";
    out->compositor_target = "text-login-fallback-deadline";
    out->deadline_policy = "fallback-deadline-declarative";
    out->deadline_text_login = 1;
    out->deadline_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "deadline-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (sync_plan->sync_credential_panel && sync_plan->sync_credential_input &&
      sync_plan->sync_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->deadline_ticket = "credential-screen-deadline-ticket";
    out->compositor_target = "credential-screen-deadline";
    out->deadline_credential_panel = 1;
    out->deadline_credential_input = 1;
    out->deadline_credential_focus = 1;
    out->deadline_text_login = 0;
    out->deadline_text_login_fallback = 0;
    out->state = "deadline-credential-ready";
    out->message = "Credential deadline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (sync_plan->sync_text_recovery && out->recovery_text_session_required) {
    out->deadline_ticket = "text-recovery-deadline-ticket";
    out->compositor_target = "text-recovery-deadline";
    out->deadline_text_recovery = 1;
    out->deadline_text_login = 1;
    out->deadline_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "deadline-text-recovery-ready";
    out->message = "Text recovery deadline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (sync_plan->sync_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->deadline_ticket = "text-login-resume-deadline-ticket";
    out->compositor_target = "text-login-resume-deadline";
    out->deadline_policy = "full-deadline-declarative";
    out->deadline_text_login = 1;
    out->deadline_text_login_resume = 1;
    out->deadline_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "deadline-resume-ready";
    out->message = "Text login resume deadline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->deadline_ticket = "text-login-fallback-deadline-ticket";
  out->compositor_target = "text-login-fallback-deadline";
  out->deadline_policy = "fallback-deadline-declarative";
  out->deadline_text_login = 1;
  out->deadline_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "deadline-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
