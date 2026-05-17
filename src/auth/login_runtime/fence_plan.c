/*
 * src/auth/login_runtime/fence_plan.c
 *
 * Credential-screen fence plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.25 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the fence stage of the credential pipeline:
 *
 *   - login_window_credential_screen_fence_plan_reset (static)
 *   - login_window_credential_screen_fence_plan_barrier_is_safe (static)
 *   - login_window_credential_screen_fence_plan_build
 *
 * The fence-plan converts a fail-closed barrier-plan into a fence
 * contract for the downstream timeline stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_barrier_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_fence_plan_reset(
    struct login_window_credential_screen_fence_plan *out,
    int barrier_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_FENCE_PLAN_VERSION;
  out->barrier_plan_available = barrier_plan_available ? 1 : 0;
  out->barrier_plan_safe = 0;
  out->fence_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->barrier_allowed = 0;
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
  out->fence_required = 1;
  out->fence_allowed = 0;
  out->fence_submitted = 0;
  out->fence_ticket_selected = 0;
  out->fence_target_selected = 0;
  out->fence_wait_required = 1;
  out->fence_wait_allowed = 0;
  out->fence_wait_submitted = 0;
  out->fence_signal_allowed = 0;
  out->fence_signal_submitted = 0;
  out->fence_fd_export_allowed = 0;
  out->fence_fd_exported = 0;
  out->fence_cpu_gpu_sync_allowed = 0;
  out->fence_cpu_gpu_sync_submitted = 0;
  out->fence_credential_panel = 0;
  out->fence_credential_input = 0;
  out->fence_credential_focus = 0;
  out->fence_text_recovery = 0;
  out->fence_text_login = 1;
  out->fence_text_login_resume = 0;
  out->fence_text_login_fallback = 1;
  out->fence_error = 1;
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
  out->barrier_ticket = "text-login-fallback-barrier-ticket";
  out->fence_ticket = "text-login-fallback-fence-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->barrier_policy = "barrier-disabled";
  out->fence_policy = "fence-disabled";
  out->event_type = "credential-screen-fence-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "barrier-plan-unavailable";
}

static int login_window_credential_screen_fence_plan_barrier_is_safe(
    const struct login_window_credential_screen_barrier_plan *barrier_plan) {
  return barrier_plan->barrier_plan_safe && barrier_plan->barrier_required &&
         barrier_plan->barrier_allowed && !barrier_plan->barrier_submitted &&
         barrier_plan->barrier_ticket_selected && barrier_plan->barrier_target_selected &&
         barrier_plan->barrier_memory_visibility_required &&
         !barrier_plan->barrier_memory_visibility_established &&
         barrier_plan->barrier_cache_visibility_required &&
         !barrier_plan->barrier_cache_visibility_established &&
         !barrier_plan->barrier_cpu_gpu_sync_allowed &&
         !barrier_plan->barrier_cpu_gpu_sync_submitted && !barrier_plan->barrier_error &&
         barrier_plan->flush_allowed && !barrier_plan->flush_submitted &&
         !barrier_plan->flush_cache_clean_allowed && !barrier_plan->flush_cache_cleaned &&
         !barrier_plan->flush_memory_barrier_allowed &&
         !barrier_plan->flush_memory_barrier_submitted &&
         !barrier_plan->framebuffer_submitted && !barrier_plan->framebuffer_mapped &&
         !barrier_plan->framebuffer_write_allowed && !barrier_plan->framebuffer_written &&
         !barrier_plan->framebuffer_flushed && !barrier_plan->framebuffer_cache_cleaned &&
         !barrier_plan->blit_submitted && !barrier_plan->blit_source_buffer_mapped &&
         !barrier_plan->blit_destination_buffer_mapped && !barrier_plan->blit_pixels_copied &&
         !barrier_plan->blit_dma_allowed && !barrier_plan->blit_dma_submitted &&
         !barrier_plan->output_submitted && !barrier_plan->output_buffer_attached &&
         !barrier_plan->output_buffer_submitted && !barrier_plan->output_flip_allowed &&
         !barrier_plan->output_flip_submitted && !barrier_plan->display_submitted &&
         !barrier_plan->display_buffer_attached && !barrier_plan->display_buffer_submitted &&
         !barrier_plan->display_mode_committed && !barrier_plan->display_flip_allowed &&
         !barrier_plan->display_flip_submitted && !barrier_plan->scanout_submitted &&
         !barrier_plan->scanout_buffer_attached && !barrier_plan->scanout_buffer_submitted &&
         !barrier_plan->vsync_submitted && !barrier_plan->vsync_wait_submitted &&
         !barrier_plan->vsync_fence_armed && !barrier_plan->schedule_submitted &&
         !barrier_plan->present_submitted && !barrier_plan->damage_submitted &&
         !barrier_plan->compositor_damage_submitted && !barrier_plan->frame_timer_armed &&
         !barrier_plan->compositor_wake_allowed && !barrier_plan->compositor_wake_submitted &&
         !barrier_plan->page_flip_allowed && !barrier_plan->page_flip_submitted &&
         barrier_plan->route_selected && !barrier_plan->route_blocked &&
         barrier_plan->credential_session_safe && barrier_plan->credential_storage_wiped &&
         barrier_plan->credential_redacted && barrier_plan->length_redacted &&
         !barrier_plan->raw_secret_exposed && !barrier_plan->masked_text_exposed &&
         barrier_plan->submit_blocked && !barrier_plan->submit_enabled &&
         !barrier_plan->auth_attempt_allowed && !barrier_plan->submit_callback_bound &&
         !barrier_plan->auth_callback_bound && barrier_plan->text_login_authoritative;
}

int login_window_credential_screen_fence_plan_build(
    const struct login_window_credential_screen_barrier_plan *barrier_plan,
    struct login_window_credential_screen_fence_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_fence_plan_reset(out, barrier_plan ? 1 : 0);
  if (!barrier_plan) return 0;
  out->requested_action = barrier_plan->requested_action;
  out->barrier_plan_safe = barrier_plan->barrier_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_fence_plan_barrier_is_safe(barrier_plan)) {
    out->event_type = "credential-screen-fence-plan-unsafe";
    out->blocked_reason = "credential-fence-plan-unsafe";
    out->message = "Credential screen fence plan unsafe; use text login.";
    return 0;
  }
  out->fence_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = barrier_plan->action_allowed ? 1 : 0;
  out->action_blocked = barrier_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = barrier_plan->input_focus_allowed ? 1 : 0;
  out->barrier_allowed = 1;
  out->fence_allowed = 1;
  out->fence_ticket_selected = 1;
  out->fence_target_selected = 1;
  out->fence_error = 0;
  out->recovery_text_session_required = barrier_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = barrier_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = barrier_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = barrier_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = barrier_plan->view;
  out->widget_tree = barrier_plan->widget_tree;
  out->barrier_ticket = barrier_plan->barrier_ticket;
  out->focus_target = barrier_plan->focus_target;
  out->primary_action = barrier_plan->primary_action;
  out->route = barrier_plan->route;
  out->compositor_target = barrier_plan->compositor_target;
  out->barrier_policy = barrier_plan->barrier_policy;
  out->fence_policy = "declarative-fence-no-wait";
  out->event_type = "credential-screen-fence-plan-ready";
  out->state = "fence-ready";
  out->message = "Credential screen fence ticket ready; no fence armed or waited.";
  out->blocked_reason = barrier_plan->blocked_reason;
  if (barrier_plan->submit_requested) {
    out->fence_ticket = "text-login-fallback-fence-ticket";
    out->compositor_target = "text-login-fallback-fence";
    out->fence_policy = "fallback-fence-declarative";
    out->fence_text_login = 1;
    out->fence_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "fence-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (barrier_plan->barrier_credential_panel && barrier_plan->barrier_credential_input &&
      barrier_plan->barrier_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->fence_ticket = "credential-screen-fence-ticket";
    out->compositor_target = "credential-screen-fence";
    out->fence_credential_panel = 1;
    out->fence_credential_input = 1;
    out->fence_credential_focus = 1;
    out->fence_text_login = 0;
    out->fence_text_login_fallback = 0;
    out->state = "fence-credential-ready";
    out->message = "Credential fence ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (barrier_plan->barrier_text_recovery && out->recovery_text_session_required) {
    out->fence_ticket = "text-recovery-fence-ticket";
    out->compositor_target = "text-recovery-fence";
    out->fence_text_recovery = 1;
    out->fence_text_login = 1;
    out->fence_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "fence-text-recovery-ready";
    out->message = "Text recovery fence ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (barrier_plan->barrier_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->fence_ticket = "text-login-resume-fence-ticket";
    out->compositor_target = "text-login-resume-fence";
    out->fence_policy = "full-fence-declarative";
    out->fence_text_login = 1;
    out->fence_text_login_resume = 1;
    out->fence_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "fence-resume-ready";
    out->message = "Text login resume fence ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->fence_ticket = "text-login-fallback-fence-ticket";
  out->compositor_target = "text-login-fallback-fence";
  out->fence_policy = "fallback-fence-declarative";
  out->fence_text_login = 1;
  out->fence_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "fence-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
