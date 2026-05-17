/*
 * src/auth/login_runtime/output_plan.c
 *
 * Credential-screen output plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.20 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the output stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_output_plan_reset (static)
 *   - login_window_credential_screen_output_plan_build
 *
 * The output-plan converts a fail-closed display-plan into an
 * output contract for the downstream blit stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_output_plan_reset(
    struct login_window_credential_screen_output_plan *out,
    int display_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_OUTPUT_PLAN_VERSION;
  out->display_plan_available = display_plan_available ? 1 : 0;
  out->display_plan_safe = 0;
  out->output_plan_safe = 0;
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
  out->output_credential_panel = 0;
  out->output_credential_input = 0;
  out->output_credential_focus = 0;
  out->output_text_recovery = 0;
  out->output_text_login = 1;
  out->output_text_login_resume = 0;
  out->output_text_login_fallback = 1;
  out->output_error = 1;
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
  out->event_type = "credential-screen-output-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "display-plan-unavailable";
}

int login_window_credential_screen_output_plan_build(
    const struct login_window_credential_screen_display_plan *display_plan,
    struct login_window_credential_screen_output_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_output_plan_reset(out, display_plan ? 1 : 0);
  if (!display_plan) return 0;
  out->requested_action = display_plan->requested_action;
  out->display_plan_safe = display_plan->display_plan_safe ? 1 : 0;
  safe = display_plan->display_plan_safe && display_plan->display_required &&
         display_plan->display_allowed && !display_plan->display_submitted &&
         display_plan->display_ticket_selected && display_plan->display_target_selected &&
         !display_plan->display_buffer_attached && !display_plan->display_buffer_submitted &&
         display_plan->display_mode_required && !display_plan->display_mode_committed &&
         !display_plan->display_flip_allowed && !display_plan->display_flip_submitted &&
         !display_plan->display_error && display_plan->scanout_required &&
         display_plan->scanout_allowed && !display_plan->scanout_submitted &&
         display_plan->scanout_ticket_selected && display_plan->scanout_target_selected &&
         !display_plan->scanout_buffer_attached && !display_plan->scanout_buffer_submitted &&
         display_plan->vsync_required && display_plan->vsync_allowed &&
         !display_plan->vsync_submitted && display_plan->vsync_ticket_selected &&
         !display_plan->vsync_wait_allowed && !display_plan->vsync_wait_submitted &&
         display_plan->vsync_fence_required && !display_plan->vsync_fence_armed &&
         display_plan->schedule_required && display_plan->schedule_allowed &&
         !display_plan->schedule_submitted && display_plan->schedule_ticket_selected &&
         display_plan->present_required && display_plan->present_allowed &&
         !display_plan->present_submitted && display_plan->present_ticket_selected &&
         display_plan->damage_required && display_plan->damage_allowed &&
         !display_plan->damage_submitted && display_plan->compositor_surface_allowed &&
         !display_plan->compositor_surface_submitted &&
         display_plan->compositor_damage_planned &&
         display_plan->compositor_damage_allowed &&
         !display_plan->compositor_damage_submitted &&
         display_plan->frame_pacing_required && display_plan->frame_pacing_allowed &&
         !display_plan->frame_timer_armed && !display_plan->compositor_wake_allowed &&
         !display_plan->compositor_wake_submitted && !display_plan->page_flip_allowed &&
         !display_plan->page_flip_submitted && display_plan->route_selected &&
         !display_plan->route_blocked && display_plan->credential_session_safe &&
         display_plan->credential_storage_wiped && display_plan->credential_redacted &&
         display_plan->length_redacted && !display_plan->raw_secret_exposed &&
         !display_plan->masked_text_exposed && display_plan->submit_blocked &&
         !display_plan->submit_enabled && !display_plan->auth_attempt_allowed &&
         !display_plan->submit_callback_bound && !display_plan->auth_callback_bound &&
         display_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-output-plan-unsafe";
    out->blocked_reason = "credential-output-plan-unsafe";
    out->message = "Credential screen output plan unsafe; use text login.";
    return 0;
  }
  out->output_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = display_plan->action_allowed ? 1 : 0;
  out->action_blocked = display_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = display_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = display_plan->damage_required ? 1 : 0;
  out->damage_allowed = display_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = display_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = display_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = display_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = display_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = display_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = display_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = display_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = display_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = display_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = display_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = display_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = display_plan->schedule_reuse_allowed ? 1 : 0;
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
  out->output_error = 0;
  out->recovery_text_session_required = display_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = display_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = display_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = display_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = display_plan->view;
  out->widget_tree = display_plan->widget_tree;
  out->damage_ticket = display_plan->damage_ticket;
  out->present_ticket = display_plan->present_ticket;
  out->schedule_ticket = display_plan->schedule_ticket;
  out->vsync_ticket = display_plan->vsync_ticket;
  out->scanout_ticket = display_plan->scanout_ticket;
  out->display_ticket = display_plan->display_ticket;
  out->focus_target = display_plan->focus_target;
  out->primary_action = display_plan->primary_action;
  out->route = display_plan->route;
  out->compositor_target = display_plan->compositor_target;
  out->damage_policy = display_plan->damage_policy;
  out->cache_policy = display_plan->cache_policy;
  out->present_policy = display_plan->present_policy;
  out->schedule_policy = display_plan->schedule_policy;
  out->vsync_policy = display_plan->vsync_policy;
  out->scanout_policy = display_plan->scanout_policy;
  out->display_policy = display_plan->display_policy;
  out->output_policy = out->schedule_incremental_allowed ?
      "incremental-output-declarative" : "full-output-declarative";
  out->event_type = "credential-screen-output-plan-ready";
  out->state = "output-ready";
  out->message = "Credential screen output ticket ready; no visual output submitted.";
  out->blocked_reason = display_plan->blocked_reason;
  if (display_plan->submit_requested) {
    out->output_ticket = "text-login-fallback-output-ticket";
    out->compositor_target = "text-login-fallback-output";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->display_policy = "fallback-display-declarative";
    out->output_policy = "fallback-output-declarative";
    out->output_text_login = 1;
    out->output_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "output-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (display_plan->display_credential_panel && display_plan->display_credential_input &&
      display_plan->display_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->output_ticket = "credential-screen-output-ticket";
    out->compositor_target = "credential-screen-output";
    out->output_credential_panel = 1;
    out->output_credential_input = 1;
    out->output_credential_focus = 1;
    out->output_text_login = 0;
    out->output_text_login_fallback = 0;
    out->state = "output-credential-ready";
    out->message = "Credential output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (display_plan->display_text_recovery && out->recovery_text_session_required) {
    out->output_ticket = "text-recovery-output-ticket";
    out->compositor_target = "text-recovery-output";
    out->output_text_recovery = 1;
    out->output_text_login = 1;
    out->output_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "output-text-recovery-ready";
    out->message = "Text recovery output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (display_plan->display_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->output_ticket = "text-login-resume-output-ticket";
    out->compositor_target = "text-login-resume-output";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->vsync_policy = "full-vsync-declarative";
    out->scanout_policy = "full-scanout-declarative";
    out->display_policy = "full-display-declarative";
    out->output_policy = "full-output-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->output_text_login = 1;
    out->output_text_login_resume = 1;
    out->output_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "output-resume-ready";
    out->message = "Text login resume output ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->output_ticket = "text-login-fallback-output-ticket";
  out->compositor_target = "text-login-fallback-output";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->display_policy = "fallback-display-declarative";
  out->output_policy = "fallback-output-declarative";
  out->output_text_login = 1;
  out->output_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "output-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
