/*
 * src/auth/login_runtime/blit_plan.c
 *
 * Credential-screen blit plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.21 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the blit stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_blit_plan_reset (static)
 *   - login_window_credential_screen_blit_plan_build
 *
 * The blit-plan converts a fail-closed output-plan into a blit
 * contract for the downstream framebuffer stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_blit_plan_reset(
    struct login_window_credential_screen_blit_plan *out,
    int output_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_BLIT_PLAN_VERSION;
  out->output_plan_available = output_plan_available ? 1 : 0;
  out->output_plan_safe = 0;
  out->blit_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->compositor_surface_allowed = 0;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 0;
  out->compositor_damage_allowed = 0;
  out->compositor_damage_submitted = 0;
  out->damage_required = 1;
  out->damage_allowed = 0;
  out->damage_submitted = 0;
  out->damage_incremental_allowed = 0;
  out->full_damage_required = 1;
  out->damage_cache_allowed = 0;
  out->damage_cache_hit = 0;
  out->damage_reuse_allowed = 0;
  out->present_required = 1;
  out->present_allowed = 0;
  out->present_submitted = 0;
  out->present_ticket_selected = 0;
  out->present_incremental_allowed = 0;
  out->full_present_required = 1;
  out->present_cache_allowed = 0;
  out->present_cache_hit = 0;
  out->present_reuse_allowed = 0;
  out->schedule_required = 1;
  out->schedule_allowed = 0;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 0;
  out->schedule_incremental_allowed = 0;
  out->full_schedule_required = 1;
  out->schedule_cache_allowed = 0;
  out->schedule_cache_hit = 0;
  out->schedule_reuse_allowed = 0;
  out->frame_pacing_required = 1;
  out->frame_pacing_allowed = 0;
  out->frame_timer_armed = 0;
  out->compositor_wake_allowed = 0;
  out->compositor_wake_submitted = 0;
  out->page_flip_allowed = 0;
  out->page_flip_submitted = 0;
  out->vsync_required = 1;
  out->vsync_allowed = 0;
  out->vsync_submitted = 0;
  out->vsync_ticket_selected = 0;
  out->vsync_wait_allowed = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_required = 1;
  out->vsync_fence_armed = 0;
  out->scanout_required = 1;
  out->scanout_allowed = 0;
  out->scanout_submitted = 0;
  out->scanout_ticket_selected = 0;
  out->scanout_target_selected = 0;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->display_required = 1;
  out->display_allowed = 0;
  out->display_submitted = 0;
  out->display_ticket_selected = 0;
  out->display_target_selected = 0;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->output_required = 1;
  out->output_allowed = 0;
  out->output_submitted = 0;
  out->output_ticket_selected = 0;
  out->output_target_selected = 0;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->blit_required = 1;
  out->blit_allowed = 0;
  out->blit_submitted = 0;
  out->blit_ticket_selected = 0;
  out->blit_target_selected = 0;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->blit_credential_panel = 0;
  out->blit_credential_input = 0;
  out->blit_credential_focus = 0;
  out->blit_text_recovery = 0;
  out->blit_text_login = 1;
  out->blit_text_login_resume = 0;
  out->blit_text_login_fallback = 1;
  out->blit_error = 1;
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
  out->damage_ticket = "text-login-fallback-damage-ticket";
  out->present_ticket = "text-login-fallback-present-ticket";
  out->schedule_ticket = "text-login-fallback-schedule-ticket";
  out->vsync_ticket = "text-login-fallback-vsync-ticket";
  out->scanout_ticket = "text-login-fallback-scanout-ticket";
  out->display_ticket = "text-login-fallback-display-ticket";
  out->output_ticket = "text-login-fallback-output-ticket";
  out->blit_ticket = "text-login-fallback-blit-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->display_policy = "display-disabled";
  out->output_policy = "output-disabled";
  out->blit_policy = "blit-disabled";
  out->event_type = "credential-screen-blit-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "output-plan-unavailable";
}

int login_window_credential_screen_blit_plan_build(
    const struct login_window_credential_screen_output_plan *output_plan,
    struct login_window_credential_screen_blit_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_blit_plan_reset(out, output_plan ? 1 : 0);
  if (!output_plan) return 0;
  out->requested_action = output_plan->requested_action;
  out->output_plan_safe = output_plan->output_plan_safe ? 1 : 0;
  safe = output_plan->output_plan_safe && output_plan->output_required &&
         output_plan->output_allowed && !output_plan->output_submitted &&
         output_plan->output_ticket_selected && output_plan->output_target_selected &&
         !output_plan->output_buffer_attached && !output_plan->output_buffer_submitted &&
         !output_plan->output_flip_allowed && !output_plan->output_flip_submitted &&
         !output_plan->output_error && output_plan->display_required &&
         output_plan->display_allowed && !output_plan->display_submitted &&
         output_plan->display_ticket_selected && output_plan->display_target_selected &&
         !output_plan->display_buffer_attached && !output_plan->display_buffer_submitted &&
         output_plan->display_mode_required && !output_plan->display_mode_committed &&
         !output_plan->display_flip_allowed && !output_plan->display_flip_submitted &&
         output_plan->scanout_required && output_plan->scanout_allowed &&
         !output_plan->scanout_submitted && output_plan->scanout_ticket_selected &&
         output_plan->scanout_target_selected && !output_plan->scanout_buffer_attached &&
         !output_plan->scanout_buffer_submitted && output_plan->vsync_required &&
         output_plan->vsync_allowed && !output_plan->vsync_submitted &&
         output_plan->vsync_ticket_selected && !output_plan->vsync_wait_allowed &&
         !output_plan->vsync_wait_submitted && output_plan->vsync_fence_required &&
         !output_plan->vsync_fence_armed && output_plan->schedule_required &&
         output_plan->schedule_allowed && !output_plan->schedule_submitted &&
         output_plan->schedule_ticket_selected && output_plan->present_required &&
         output_plan->present_allowed && !output_plan->present_submitted &&
         output_plan->present_ticket_selected && output_plan->damage_required &&
         output_plan->damage_allowed && !output_plan->damage_submitted &&
         output_plan->compositor_surface_allowed &&
         !output_plan->compositor_surface_submitted &&
         output_plan->compositor_damage_planned &&
         output_plan->compositor_damage_allowed &&
         !output_plan->compositor_damage_submitted &&
         output_plan->frame_pacing_required && output_plan->frame_pacing_allowed &&
         !output_plan->frame_timer_armed && !output_plan->compositor_wake_allowed &&
         !output_plan->compositor_wake_submitted && !output_plan->page_flip_allowed &&
         !output_plan->page_flip_submitted && output_plan->route_selected &&
         !output_plan->route_blocked && output_plan->credential_session_safe &&
         output_plan->credential_storage_wiped && output_plan->credential_redacted &&
         output_plan->length_redacted && !output_plan->raw_secret_exposed &&
         !output_plan->masked_text_exposed && output_plan->submit_blocked &&
         !output_plan->submit_enabled && !output_plan->auth_attempt_allowed &&
         !output_plan->submit_callback_bound && !output_plan->auth_callback_bound &&
         output_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-blit-plan-unsafe";
    out->blocked_reason = "credential-blit-plan-unsafe";
    out->message = "Credential screen blit plan unsafe; use text login.";
    return 0;
  }
  out->blit_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = output_plan->action_allowed ? 1 : 0;
  out->action_blocked = output_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = output_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = output_plan->damage_required ? 1 : 0;
  out->damage_allowed = output_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = output_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = output_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = output_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = output_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = output_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = output_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = output_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = output_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = output_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = output_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = output_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = output_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = 1;
  out->vsync_submitted = 0;
  out->vsync_ticket_selected = 1;
  out->vsync_wait_allowed = 0;
  out->vsync_wait_submitted = 0;
  out->vsync_fence_required = 1;
  out->vsync_fence_armed = 0;
  out->scanout_allowed = 1;
  out->scanout_submitted = 0;
  out->scanout_ticket_selected = 1;
  out->scanout_target_selected = 1;
  out->scanout_buffer_attached = 0;
  out->scanout_buffer_submitted = 0;
  out->display_allowed = 1;
  out->display_submitted = 0;
  out->display_ticket_selected = 1;
  out->display_target_selected = 1;
  out->display_buffer_attached = 0;
  out->display_buffer_submitted = 0;
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->display_flip_allowed = 0;
  out->display_flip_submitted = 0;
  out->output_allowed = 1;
  out->output_submitted = 0;
  out->output_ticket_selected = 1;
  out->output_target_selected = 1;
  out->output_buffer_attached = 0;
  out->output_buffer_submitted = 0;
  out->output_flip_allowed = 0;
  out->output_flip_submitted = 0;
  out->blit_allowed = 1;
  out->blit_submitted = 0;
  out->blit_ticket_selected = 1;
  out->blit_target_selected = 1;
  out->blit_source_buffer_mapped = 0;
  out->blit_destination_buffer_mapped = 0;
  out->blit_pixels_copied = 0;
  out->blit_dma_allowed = 0;
  out->blit_dma_submitted = 0;
  out->blit_error = 0;
  out->recovery_text_session_required = output_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = output_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = output_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = output_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = output_plan->view;
  out->widget_tree = output_plan->widget_tree;
  out->damage_ticket = output_plan->damage_ticket;
  out->present_ticket = output_plan->present_ticket;
  out->schedule_ticket = output_plan->schedule_ticket;
  out->vsync_ticket = output_plan->vsync_ticket;
  out->scanout_ticket = output_plan->scanout_ticket;
  out->display_ticket = output_plan->display_ticket;
  out->output_ticket = output_plan->output_ticket;
  out->focus_target = output_plan->focus_target;
  out->primary_action = output_plan->primary_action;
  out->route = output_plan->route;
  out->compositor_target = output_plan->compositor_target;
  out->damage_policy = output_plan->damage_policy;
  out->cache_policy = output_plan->cache_policy;
  out->present_policy = output_plan->present_policy;
  out->schedule_policy = output_plan->schedule_policy;
  out->vsync_policy = output_plan->vsync_policy;
  out->scanout_policy = output_plan->scanout_policy;
  out->display_policy = output_plan->display_policy;
  out->output_policy = output_plan->output_policy;
  out->blit_policy = out->schedule_incremental_allowed ?
      "incremental-blit-declarative" : "full-blit-declarative";
  out->event_type = "credential-screen-blit-plan-ready";
  out->state = "blit-ready";
  out->message = "Credential screen blit ticket ready; no pixels copied.";
  out->blocked_reason = output_plan->blocked_reason;
  if (output_plan->submit_requested) {
    out->blit_ticket = "text-login-fallback-blit-ticket";
    out->compositor_target = "text-login-fallback-blit";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->display_policy = "fallback-display-declarative";
    out->output_policy = "fallback-output-declarative";
    out->blit_policy = "fallback-blit-declarative";
    out->blit_text_login = 1;
    out->blit_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "blit-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (output_plan->output_credential_panel && output_plan->output_credential_input &&
      output_plan->output_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->blit_ticket = "credential-screen-blit-ticket";
    out->compositor_target = "credential-screen-blit";
    out->blit_credential_panel = 1;
    out->blit_credential_input = 1;
    out->blit_credential_focus = 1;
    out->blit_text_login = 0;
    out->blit_text_login_fallback = 0;
    out->state = "blit-credential-ready";
    out->message = "Credential blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (output_plan->output_text_recovery && out->recovery_text_session_required) {
    out->blit_ticket = "text-recovery-blit-ticket";
    out->compositor_target = "text-recovery-blit";
    out->blit_text_recovery = 1;
    out->blit_text_login = 1;
    out->blit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "blit-text-recovery-ready";
    out->message = "Text recovery blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (output_plan->output_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->blit_ticket = "text-login-resume-blit-ticket";
    out->compositor_target = "text-login-resume-blit";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->vsync_policy = "full-vsync-declarative";
    out->scanout_policy = "full-scanout-declarative";
    out->display_policy = "full-display-declarative";
    out->output_policy = "full-output-declarative";
    out->blit_policy = "full-blit-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->blit_text_login = 1;
    out->blit_text_login_resume = 1;
    out->blit_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "blit-resume-ready";
    out->message = "Text login resume blit ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->blit_ticket = "text-login-fallback-blit-ticket";
  out->compositor_target = "text-login-fallback-blit";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->display_policy = "fallback-display-declarative";
  out->output_policy = "fallback-output-declarative";
  out->blit_policy = "fallback-blit-declarative";
  out->blit_text_login = 1;
  out->blit_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "blit-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
