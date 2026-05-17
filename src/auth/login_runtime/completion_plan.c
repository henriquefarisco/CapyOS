/*
 * src/auth/login_runtime/completion_plan.c
 *
 * Credential-screen completion plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.29 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the completion stage of the credential pipeline:
 *
 *   - login_window_credential_screen_completion_plan_reset (static)
 *   - login_window_credential_screen_completion_plan_deadline_is_safe (static)
 *   - login_window_credential_screen_completion_plan_build
 *
 * The completion-plan converts a fail-closed deadline-plan into a
 * completion contract for the downstream ack stage.  The static
 * `_reset` helper is the canonical "blocked" initializer and the
 * static `_deadline_is_safe` predicate consolidates the upstream-
 * safety checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_completion_plan_reset(
    struct login_window_credential_screen_completion_plan *out,
    int deadline_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPLETION_PLAN_VERSION;
  out->deadline_plan_available = deadline_plan_available ? 1 : 0;
  out->deadline_plan_safe = 0;
  out->completion_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
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
  out->completion_credential_panel = 0;
  out->completion_credential_input = 0;
  out->completion_credential_focus = 0;
  out->completion_text_recovery = 0;
  out->completion_text_login = 1;
  out->completion_text_login_resume = 0;
  out->completion_text_login_fallback = 1;
  out->completion_error = 1;
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
  out->deadline_ticket = "text-login-fallback-deadline-ticket";
  out->completion_ticket = "text-login-fallback-completion-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->deadline_policy = "deadline-disabled";
  out->completion_policy = "completion-disabled";
  out->event_type = "credential-screen-completion-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "deadline-plan-unavailable";
}

static int login_window_credential_screen_completion_plan_deadline_is_safe(
    const struct login_window_credential_screen_deadline_plan *deadline_plan) {
  return deadline_plan->deadline_plan_safe && deadline_plan->deadline_required &&
         deadline_plan->deadline_allowed && !deadline_plan->deadline_armed &&
         deadline_plan->deadline_ticket_selected && deadline_plan->deadline_target_selected &&
         deadline_plan->deadline_timer_required && !deadline_plan->deadline_timer_armed &&
         !deadline_plan->deadline_expired && deadline_plan->deadline_completion_required &&
         !deadline_plan->deadline_completion_reported &&
         !deadline_plan->deadline_cpu_gpu_sync_allowed &&
         !deadline_plan->deadline_cpu_gpu_sync_submitted && !deadline_plan->deadline_error &&
         deadline_plan->sync_allowed && !deadline_plan->sync_submitted &&
         !deadline_plan->sync_wait_allowed && !deadline_plan->sync_wait_submitted &&
         !deadline_plan->sync_signal_allowed && !deadline_plan->sync_signal_submitted &&
         !deadline_plan->sync_deadline_armed && !deadline_plan->sync_completion_reported &&
         !deadline_plan->sync_cpu_gpu_sync_allowed &&
         !deadline_plan->sync_cpu_gpu_sync_submitted && !deadline_plan->timeline_submitted &&
         !deadline_plan->timeline_wait_allowed && !deadline_plan->timeline_wait_submitted &&
         !deadline_plan->timeline_signal_allowed && !deadline_plan->timeline_signal_submitted &&
         !deadline_plan->timeline_semaphore_allowed && !deadline_plan->timeline_semaphore_submitted &&
         !deadline_plan->timeline_value_allocated && !deadline_plan->timeline_value_published &&
         !deadline_plan->timeline_cpu_gpu_sync_allowed &&
         !deadline_plan->timeline_cpu_gpu_sync_submitted && !deadline_plan->fence_submitted &&
         !deadline_plan->fence_wait_allowed && !deadline_plan->fence_wait_submitted &&
         !deadline_plan->fence_signal_allowed && !deadline_plan->fence_signal_submitted &&
         !deadline_plan->fence_fd_export_allowed && !deadline_plan->fence_fd_exported &&
         !deadline_plan->fence_cpu_gpu_sync_allowed &&
         !deadline_plan->fence_cpu_gpu_sync_submitted && !deadline_plan->barrier_submitted &&
         !deadline_plan->barrier_memory_visibility_established &&
         !deadline_plan->barrier_cache_visibility_established &&
         !deadline_plan->barrier_cpu_gpu_sync_allowed &&
         !deadline_plan->barrier_cpu_gpu_sync_submitted && !deadline_plan->flush_submitted &&
         !deadline_plan->flush_cache_clean_allowed && !deadline_plan->flush_cache_cleaned &&
         !deadline_plan->flush_memory_barrier_allowed &&
         !deadline_plan->flush_memory_barrier_submitted && !deadline_plan->framebuffer_submitted &&
         !deadline_plan->framebuffer_mapped && !deadline_plan->framebuffer_write_allowed &&
         !deadline_plan->framebuffer_written && !deadline_plan->framebuffer_flushed &&
         !deadline_plan->framebuffer_cache_cleaned && !deadline_plan->blit_submitted &&
         !deadline_plan->blit_source_buffer_mapped && !deadline_plan->blit_destination_buffer_mapped &&
         !deadline_plan->blit_pixels_copied && !deadline_plan->blit_dma_allowed &&
         !deadline_plan->blit_dma_submitted && !deadline_plan->output_submitted &&
         !deadline_plan->output_buffer_attached && !deadline_plan->output_buffer_submitted &&
         !deadline_plan->output_flip_allowed && !deadline_plan->output_flip_submitted &&
         !deadline_plan->display_submitted && !deadline_plan->display_buffer_attached &&
         !deadline_plan->display_buffer_submitted && !deadline_plan->display_mode_committed &&
         !deadline_plan->display_flip_allowed && !deadline_plan->display_flip_submitted &&
         !deadline_plan->scanout_submitted && !deadline_plan->scanout_buffer_attached &&
         !deadline_plan->scanout_buffer_submitted && !deadline_plan->vsync_submitted &&
         !deadline_plan->vsync_wait_submitted && !deadline_plan->vsync_fence_armed &&
         !deadline_plan->schedule_submitted && !deadline_plan->present_submitted &&
         !deadline_plan->damage_submitted && !deadline_plan->compositor_damage_submitted &&
         !deadline_plan->frame_timer_armed && !deadline_plan->compositor_wake_allowed &&
         !deadline_plan->compositor_wake_submitted && !deadline_plan->page_flip_allowed &&
         !deadline_plan->page_flip_submitted && deadline_plan->route_selected &&
         !deadline_plan->route_blocked && deadline_plan->credential_session_safe &&
         deadline_plan->credential_storage_wiped && deadline_plan->credential_redacted &&
         deadline_plan->length_redacted && !deadline_plan->raw_secret_exposed &&
         !deadline_plan->masked_text_exposed && deadline_plan->submit_blocked &&
         !deadline_plan->submit_enabled && !deadline_plan->auth_attempt_allowed &&
         !deadline_plan->submit_callback_bound && !deadline_plan->auth_callback_bound &&
         deadline_plan->text_login_authoritative;
}

int login_window_credential_screen_completion_plan_build(
    const struct login_window_credential_screen_deadline_plan *deadline_plan,
    struct login_window_credential_screen_completion_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_completion_plan_reset(out, deadline_plan ? 1 : 0);
  if (!deadline_plan) return 0;
  out->requested_action = deadline_plan->requested_action;
  out->deadline_plan_safe = deadline_plan->deadline_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_completion_plan_deadline_is_safe(deadline_plan)) {
    out->event_type = "credential-screen-completion-plan-unsafe";
    out->blocked_reason = "credential-completion-plan-unsafe";
    out->message = "Credential screen completion plan unsafe; use text login.";
    return 0;
  }
  out->completion_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = deadline_plan->action_allowed ? 1 : 0;
  out->action_blocked = deadline_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = deadline_plan->input_focus_allowed ? 1 : 0;
  out->deadline_required = deadline_plan->deadline_required ? 1 : 0;
  out->deadline_allowed = 1;
  out->completion_allowed = 1;
  out->completion_ticket_selected = 1;
  out->completion_target_selected = 1;
  out->completion_error = 0;
  out->recovery_text_session_required = deadline_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = deadline_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = deadline_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = deadline_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = deadline_plan->view;
  out->widget_tree = deadline_plan->widget_tree;
  out->deadline_ticket = deadline_plan->deadline_ticket;
  out->focus_target = deadline_plan->focus_target;
  out->primary_action = deadline_plan->primary_action;
  out->route = deadline_plan->route;
  out->compositor_target = deadline_plan->compositor_target;
  out->deadline_policy = deadline_plan->deadline_policy;
  out->completion_policy = "declarative-completion-no-report";
  out->event_type = "credential-screen-completion-plan-ready";
  out->state = "completion-ready";
  out->message = "Credential screen completion ticket ready; no completion reported.";
  out->blocked_reason = deadline_plan->blocked_reason;
  if (deadline_plan->submit_requested) {
    out->completion_ticket = "text-login-fallback-completion-ticket";
    out->compositor_target = "text-login-fallback-completion";
    out->completion_policy = "fallback-completion-declarative";
    out->completion_text_login = 1;
    out->completion_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "completion-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (deadline_plan->deadline_credential_panel && deadline_plan->deadline_credential_input &&
      deadline_plan->deadline_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->completion_ticket = "credential-screen-completion-ticket";
    out->compositor_target = "credential-screen-completion";
    out->completion_credential_panel = 1;
    out->completion_credential_input = 1;
    out->completion_credential_focus = 1;
    out->completion_text_login = 0;
    out->completion_text_login_fallback = 0;
    out->state = "completion-credential-ready";
    out->message = "Credential completion ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (deadline_plan->deadline_text_recovery && out->recovery_text_session_required) {
    out->completion_ticket = "text-recovery-completion-ticket";
    out->compositor_target = "text-recovery-completion";
    out->completion_text_recovery = 1;
    out->completion_text_login = 1;
    out->completion_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "completion-text-recovery-ready";
    out->message = "Text recovery completion ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (deadline_plan->deadline_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->completion_ticket = "text-login-resume-completion-ticket";
    out->compositor_target = "text-login-resume-completion";
    out->completion_policy = "full-completion-declarative";
    out->completion_text_login = 1;
    out->completion_text_login_resume = 1;
    out->completion_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "completion-resume-ready";
    out->message = "Text login resume completion ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->completion_ticket = "text-login-fallback-completion-ticket";
  out->compositor_target = "text-login-fallback-completion";
  out->completion_policy = "fallback-completion-declarative";
  out->completion_text_login = 1;
  out->completion_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "completion-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
