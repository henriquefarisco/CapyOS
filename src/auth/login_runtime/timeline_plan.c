/*
 * src/auth/login_runtime/timeline_plan.c
 *
 * Credential-screen timeline plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.26 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the timeline stage of the credential pipeline:
 *
 *   - login_window_credential_screen_timeline_plan_reset (static)
 *   - login_window_credential_screen_timeline_plan_fence_is_safe (static)
 *   - login_window_credential_screen_timeline_plan_build
 *
 * The timeline-plan converts a fail-closed fence-plan into a
 * timeline contract for the downstream sync stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_fence_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_timeline_plan_reset(
    struct login_window_credential_screen_timeline_plan *out,
    int fence_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_TIMELINE_PLAN_VERSION;
  out->fence_plan_available = fence_plan_available ? 1 : 0;
  out->fence_plan_safe = 0;
  out->timeline_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->fence_allowed = 0;
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
  out->timeline_required = 1;
  out->timeline_allowed = 0;
  out->timeline_submitted = 0;
  out->timeline_ticket_selected = 0;
  out->timeline_target_selected = 0;
  out->timeline_wait_required = 1;
  out->timeline_wait_allowed = 0;
  out->timeline_wait_submitted = 0;
  out->timeline_signal_allowed = 0;
  out->timeline_signal_submitted = 0;
  out->timeline_semaphore_allowed = 0;
  out->timeline_semaphore_submitted = 0;
  out->timeline_value_required = 1;
  out->timeline_value_allocated = 0;
  out->timeline_value_published = 0;
  out->timeline_cpu_gpu_sync_allowed = 0;
  out->timeline_cpu_gpu_sync_submitted = 0;
  out->timeline_credential_panel = 0;
  out->timeline_credential_input = 0;
  out->timeline_credential_focus = 0;
  out->timeline_text_recovery = 0;
  out->timeline_text_login = 1;
  out->timeline_text_login_resume = 0;
  out->timeline_text_login_fallback = 1;
  out->timeline_error = 1;
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
  out->fence_ticket = "text-login-fallback-fence-ticket";
  out->timeline_ticket = "text-login-fallback-timeline-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->fence_policy = "fence-disabled";
  out->timeline_policy = "timeline-disabled";
  out->event_type = "credential-screen-timeline-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "fence-plan-unavailable";
}

static int login_window_credential_screen_timeline_plan_fence_is_safe(
    const struct login_window_credential_screen_fence_plan *fence_plan) {
  return fence_plan->fence_plan_safe && fence_plan->fence_required &&
         fence_plan->fence_allowed && !fence_plan->fence_submitted &&
         fence_plan->fence_ticket_selected && fence_plan->fence_target_selected &&
         fence_plan->fence_wait_required && !fence_plan->fence_wait_allowed &&
         !fence_plan->fence_wait_submitted && !fence_plan->fence_signal_allowed &&
         !fence_plan->fence_signal_submitted && !fence_plan->fence_fd_export_allowed &&
         !fence_plan->fence_fd_exported && !fence_plan->fence_cpu_gpu_sync_allowed &&
         !fence_plan->fence_cpu_gpu_sync_submitted && !fence_plan->fence_error &&
         fence_plan->barrier_allowed && !fence_plan->barrier_submitted &&
         !fence_plan->barrier_memory_visibility_established &&
         !fence_plan->barrier_cache_visibility_established &&
         !fence_plan->barrier_cpu_gpu_sync_allowed &&
         !fence_plan->barrier_cpu_gpu_sync_submitted &&
         !fence_plan->flush_submitted && !fence_plan->flush_cache_clean_allowed &&
         !fence_plan->flush_cache_cleaned && !fence_plan->flush_memory_barrier_allowed &&
         !fence_plan->flush_memory_barrier_submitted && !fence_plan->framebuffer_submitted &&
         !fence_plan->framebuffer_mapped && !fence_plan->framebuffer_write_allowed &&
         !fence_plan->framebuffer_written && !fence_plan->framebuffer_flushed &&
         !fence_plan->framebuffer_cache_cleaned && !fence_plan->blit_submitted &&
         !fence_plan->blit_source_buffer_mapped && !fence_plan->blit_destination_buffer_mapped &&
         !fence_plan->blit_pixels_copied && !fence_plan->blit_dma_allowed &&
         !fence_plan->blit_dma_submitted && !fence_plan->output_submitted &&
         !fence_plan->output_buffer_attached && !fence_plan->output_buffer_submitted &&
         !fence_plan->output_flip_allowed && !fence_plan->output_flip_submitted &&
         !fence_plan->display_submitted && !fence_plan->display_buffer_attached &&
         !fence_plan->display_buffer_submitted && !fence_plan->display_mode_committed &&
         !fence_plan->display_flip_allowed && !fence_plan->display_flip_submitted &&
         !fence_plan->scanout_submitted && !fence_plan->scanout_buffer_attached &&
         !fence_plan->scanout_buffer_submitted && !fence_plan->vsync_submitted &&
         !fence_plan->vsync_wait_submitted && !fence_plan->vsync_fence_armed &&
         !fence_plan->schedule_submitted && !fence_plan->present_submitted &&
         !fence_plan->damage_submitted && !fence_plan->compositor_damage_submitted &&
         !fence_plan->frame_timer_armed && !fence_plan->compositor_wake_allowed &&
         !fence_plan->compositor_wake_submitted && !fence_plan->page_flip_allowed &&
         !fence_plan->page_flip_submitted && fence_plan->route_selected &&
         !fence_plan->route_blocked && fence_plan->credential_session_safe &&
         fence_plan->credential_storage_wiped && fence_plan->credential_redacted &&
         fence_plan->length_redacted && !fence_plan->raw_secret_exposed &&
         !fence_plan->masked_text_exposed && fence_plan->submit_blocked &&
         !fence_plan->submit_enabled && !fence_plan->auth_attempt_allowed &&
         !fence_plan->submit_callback_bound && !fence_plan->auth_callback_bound &&
         fence_plan->text_login_authoritative;
}

int login_window_credential_screen_timeline_plan_build(
    const struct login_window_credential_screen_fence_plan *fence_plan,
    struct login_window_credential_screen_timeline_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_timeline_plan_reset(out, fence_plan ? 1 : 0);
  if (!fence_plan) return 0;
  out->requested_action = fence_plan->requested_action;
  out->fence_plan_safe = fence_plan->fence_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_timeline_plan_fence_is_safe(fence_plan)) {
    out->event_type = "credential-screen-timeline-plan-unsafe";
    out->blocked_reason = "credential-timeline-plan-unsafe";
    out->message = "Credential screen timeline plan unsafe; use text login.";
    return 0;
  }
  out->timeline_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = fence_plan->action_allowed ? 1 : 0;
  out->action_blocked = fence_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = fence_plan->input_focus_allowed ? 1 : 0;
  out->fence_allowed = 1;
  out->timeline_allowed = 1;
  out->timeline_ticket_selected = 1;
  out->timeline_target_selected = 1;
  out->timeline_error = 0;
  out->recovery_text_session_required = fence_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = fence_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = fence_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = fence_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = fence_plan->view;
  out->widget_tree = fence_plan->widget_tree;
  out->fence_ticket = fence_plan->fence_ticket;
  out->focus_target = fence_plan->focus_target;
  out->primary_action = fence_plan->primary_action;
  out->route = fence_plan->route;
  out->compositor_target = fence_plan->compositor_target;
  out->fence_policy = fence_plan->fence_policy;
  out->timeline_policy = "declarative-timeline-no-submit";
  out->event_type = "credential-screen-timeline-plan-ready";
  out->state = "timeline-ready";
  out->message = "Credential screen timeline ticket ready; no timeline submitted.";
  out->blocked_reason = fence_plan->blocked_reason;
  if (fence_plan->submit_requested) {
    out->timeline_ticket = "text-login-fallback-timeline-ticket";
    out->compositor_target = "text-login-fallback-timeline";
    out->timeline_policy = "fallback-timeline-declarative";
    out->timeline_text_login = 1;
    out->timeline_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "timeline-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (fence_plan->fence_credential_panel && fence_plan->fence_credential_input &&
      fence_plan->fence_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->timeline_ticket = "credential-screen-timeline-ticket";
    out->compositor_target = "credential-screen-timeline";
    out->timeline_credential_panel = 1;
    out->timeline_credential_input = 1;
    out->timeline_credential_focus = 1;
    out->timeline_text_login = 0;
    out->timeline_text_login_fallback = 0;
    out->state = "timeline-credential-ready";
    out->message = "Credential timeline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (fence_plan->fence_text_recovery && out->recovery_text_session_required) {
    out->timeline_ticket = "text-recovery-timeline-ticket";
    out->compositor_target = "text-recovery-timeline";
    out->timeline_text_recovery = 1;
    out->timeline_text_login = 1;
    out->timeline_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "timeline-text-recovery-ready";
    out->message = "Text recovery timeline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (fence_plan->fence_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->timeline_ticket = "text-login-resume-timeline-ticket";
    out->compositor_target = "text-login-resume-timeline";
    out->timeline_policy = "full-timeline-declarative";
    out->timeline_text_login = 1;
    out->timeline_text_login_resume = 1;
    out->timeline_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "timeline-resume-ready";
    out->message = "Text login resume timeline ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->timeline_ticket = "text-login-fallback-timeline-ticket";
  out->compositor_target = "text-login-fallback-timeline";
  out->timeline_policy = "fallback-timeline-declarative";
  out->timeline_text_login = 1;
  out->timeline_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "timeline-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
