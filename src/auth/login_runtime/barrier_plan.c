/*
 * src/auth/login_runtime/barrier_plan.c
 *
 * Credential-screen barrier plan reset + safety helper + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.24 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper, the upstream-safety predicate and the public builder for
 * the barrier stage of the credential pipeline:
 *
 *   - login_window_credential_screen_barrier_plan_reset (static)
 *   - login_window_credential_screen_barrier_plan_flush_is_safe (static)
 *   - login_window_credential_screen_barrier_plan_build
 *
 * The barrier-plan converts a fail-closed flush-plan into a barrier
 * contract for the downstream fence stage.  The static `_reset`
 * helper is the canonical "blocked" initializer and the static
 * `_flush_is_safe` predicate consolidates the upstream-safety
 * checks shared with future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_barrier_plan_reset(
    struct login_window_credential_screen_barrier_plan *out,
    int flush_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_BARRIER_PLAN_VERSION;
  out->flush_plan_available = flush_plan_available ? 1 : 0;
  out->flush_plan_safe = 0;
  out->barrier_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->flush_allowed = 0;
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
  out->barrier_required = 1;
  out->barrier_allowed = 0;
  out->barrier_submitted = 0;
  out->barrier_ticket_selected = 0;
  out->barrier_target_selected = 0;
  out->barrier_memory_visibility_required = 1;
  out->barrier_memory_visibility_established = 0;
  out->barrier_cache_visibility_required = 1;
  out->barrier_cache_visibility_established = 0;
  out->barrier_cpu_gpu_sync_allowed = 0;
  out->barrier_cpu_gpu_sync_submitted = 0;
  out->barrier_credential_panel = 0;
  out->barrier_credential_input = 0;
  out->barrier_credential_focus = 0;
  out->barrier_text_recovery = 0;
  out->barrier_text_login = 1;
  out->barrier_text_login_resume = 0;
  out->barrier_text_login_fallback = 1;
  out->barrier_error = 1;
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
  out->flush_ticket = "text-login-fallback-flush-ticket";
  out->barrier_ticket = "text-login-fallback-barrier-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->flush_policy = "flush-disabled";
  out->barrier_policy = "barrier-disabled";
  out->event_type = "credential-screen-barrier-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "flush-plan-unavailable";
}

static int login_window_credential_screen_barrier_plan_flush_is_safe(
    const struct login_window_credential_screen_flush_plan *flush_plan) {
  return flush_plan->flush_plan_safe && flush_plan->flush_required &&
         flush_plan->flush_allowed && !flush_plan->flush_submitted &&
         flush_plan->flush_ticket_selected && flush_plan->flush_target_selected &&
         flush_plan->flush_cache_clean_required &&
         !flush_plan->flush_cache_clean_allowed && !flush_plan->flush_cache_cleaned &&
         !flush_plan->flush_memory_barrier_allowed &&
         !flush_plan->flush_memory_barrier_submitted && !flush_plan->flush_error &&
         flush_plan->framebuffer_required && flush_plan->framebuffer_allowed &&
         !flush_plan->framebuffer_submitted && flush_plan->framebuffer_ticket_selected &&
         flush_plan->framebuffer_target_selected && !flush_plan->framebuffer_mapped &&
         !flush_plan->framebuffer_write_allowed && !flush_plan->framebuffer_written &&
         flush_plan->framebuffer_flush_required && !flush_plan->framebuffer_flushed &&
         flush_plan->framebuffer_cache_clean_required &&
         !flush_plan->framebuffer_cache_cleaned && flush_plan->blit_required &&
         flush_plan->blit_allowed && !flush_plan->blit_submitted &&
         flush_plan->blit_ticket_selected && flush_plan->blit_target_selected &&
         !flush_plan->blit_source_buffer_mapped &&
         !flush_plan->blit_destination_buffer_mapped && !flush_plan->blit_pixels_copied &&
         !flush_plan->blit_dma_allowed && !flush_plan->blit_dma_submitted &&
         flush_plan->output_required && flush_plan->output_allowed &&
         !flush_plan->output_submitted && flush_plan->output_ticket_selected &&
         flush_plan->output_target_selected && !flush_plan->output_buffer_attached &&
         !flush_plan->output_buffer_submitted && !flush_plan->output_flip_allowed &&
         !flush_plan->output_flip_submitted && flush_plan->display_required &&
         flush_plan->display_allowed && !flush_plan->display_submitted &&
         flush_plan->display_ticket_selected && flush_plan->display_target_selected &&
         !flush_plan->display_buffer_attached && !flush_plan->display_buffer_submitted &&
         flush_plan->display_mode_required && !flush_plan->display_mode_committed &&
         !flush_plan->display_flip_allowed && !flush_plan->display_flip_submitted &&
         flush_plan->scanout_required && flush_plan->scanout_allowed &&
         !flush_plan->scanout_submitted && flush_plan->scanout_ticket_selected &&
         flush_plan->scanout_target_selected && !flush_plan->scanout_buffer_attached &&
         !flush_plan->scanout_buffer_submitted && flush_plan->vsync_required &&
         flush_plan->vsync_allowed && !flush_plan->vsync_submitted &&
         flush_plan->vsync_ticket_selected && !flush_plan->vsync_wait_allowed &&
         !flush_plan->vsync_wait_submitted && flush_plan->vsync_fence_required &&
         !flush_plan->vsync_fence_armed && flush_plan->schedule_required &&
         flush_plan->schedule_allowed && !flush_plan->schedule_submitted &&
         flush_plan->schedule_ticket_selected && flush_plan->present_required &&
         flush_plan->present_allowed && !flush_plan->present_submitted &&
         flush_plan->present_ticket_selected && flush_plan->damage_required &&
         flush_plan->damage_allowed && !flush_plan->damage_submitted &&
         flush_plan->compositor_surface_allowed &&
         !flush_plan->compositor_surface_submitted &&
         flush_plan->compositor_damage_planned && flush_plan->compositor_damage_allowed &&
         !flush_plan->compositor_damage_submitted && flush_plan->frame_pacing_required &&
         flush_plan->frame_pacing_allowed && !flush_plan->frame_timer_armed &&
         !flush_plan->compositor_wake_allowed && !flush_plan->compositor_wake_submitted &&
         !flush_plan->page_flip_allowed && !flush_plan->page_flip_submitted &&
         flush_plan->route_selected && !flush_plan->route_blocked &&
         flush_plan->credential_session_safe &&
         flush_plan->credential_storage_wiped && flush_plan->credential_redacted &&
         flush_plan->length_redacted && !flush_plan->raw_secret_exposed &&
         !flush_plan->masked_text_exposed && flush_plan->submit_blocked &&
         !flush_plan->submit_enabled && !flush_plan->auth_attempt_allowed &&
         !flush_plan->submit_callback_bound && !flush_plan->auth_callback_bound &&
         flush_plan->text_login_authoritative;
}

int login_window_credential_screen_barrier_plan_build(
    const struct login_window_credential_screen_flush_plan *flush_plan,
    struct login_window_credential_screen_barrier_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_barrier_plan_reset(out, flush_plan ? 1 : 0);
  if (!flush_plan) return 0;
  out->requested_action = flush_plan->requested_action;
  out->flush_plan_safe = flush_plan->flush_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_barrier_plan_flush_is_safe(flush_plan)) {
    out->event_type = "credential-screen-barrier-plan-unsafe";
    out->blocked_reason = "credential-barrier-plan-unsafe";
    out->message = "Credential screen barrier plan unsafe; use text login.";
    return 0;
  }
  out->barrier_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = flush_plan->action_allowed ? 1 : 0;
  out->action_blocked = flush_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = flush_plan->input_focus_allowed ? 1 : 0;
  out->flush_allowed = 1;
  out->barrier_allowed = 1;
  out->barrier_ticket_selected = 1;
  out->barrier_target_selected = 1;
  out->barrier_error = 0;
  out->recovery_text_session_required = flush_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = flush_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = flush_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = flush_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = flush_plan->view;
  out->widget_tree = flush_plan->widget_tree;
  out->flush_ticket = flush_plan->flush_ticket;
  out->focus_target = flush_plan->focus_target;
  out->primary_action = flush_plan->primary_action;
  out->route = flush_plan->route;
  out->compositor_target = flush_plan->compositor_target;
  out->flush_policy = flush_plan->flush_policy;
  out->barrier_policy = flush_plan->schedule_incremental_allowed ?
      "incremental-barrier-declarative" : "full-barrier-declarative";
  out->event_type = "credential-screen-barrier-plan-ready";
  out->state = "barrier-ready";
  out->message = "Credential screen barrier ticket ready; no memory barrier submitted.";
  out->blocked_reason = flush_plan->blocked_reason;
  if (flush_plan->submit_requested) {
    out->barrier_ticket = "text-login-fallback-barrier-ticket";
    out->compositor_target = "text-login-fallback-barrier";
    out->barrier_policy = "fallback-barrier-declarative";
    out->barrier_text_login = 1;
    out->barrier_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "barrier-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (flush_plan->flush_credential_panel && flush_plan->flush_credential_input &&
      flush_plan->flush_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->barrier_ticket = "credential-screen-barrier-ticket";
    out->compositor_target = "credential-screen-barrier";
    out->barrier_credential_panel = 1;
    out->barrier_credential_input = 1;
    out->barrier_credential_focus = 1;
    out->barrier_text_login = 0;
    out->barrier_text_login_fallback = 0;
    out->state = "barrier-credential-ready";
    out->message = "Credential barrier ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (flush_plan->flush_text_recovery && out->recovery_text_session_required) {
    out->barrier_ticket = "text-recovery-barrier-ticket";
    out->compositor_target = "text-recovery-barrier";
    out->barrier_text_recovery = 1;
    out->barrier_text_login = 1;
    out->barrier_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "barrier-text-recovery-ready";
    out->message = "Text recovery barrier ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (flush_plan->flush_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->barrier_ticket = "text-login-resume-barrier-ticket";
    out->compositor_target = "text-login-resume-barrier";
    out->barrier_policy = "full-barrier-declarative";
    out->barrier_text_login = 1;
    out->barrier_text_login_resume = 1;
    out->barrier_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "barrier-resume-ready";
    out->message = "Text login resume barrier ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->barrier_ticket = "text-login-fallback-barrier-ticket";
  out->compositor_target = "text-login-fallback-barrier";
  out->barrier_policy = "fallback-barrier-declarative";
  out->barrier_text_login = 1;
  out->barrier_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "barrier-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
