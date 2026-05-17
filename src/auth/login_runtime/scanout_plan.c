/*
 * src/auth/login_runtime/scanout_plan.c
 *
 * Credential-screen scanout plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.18 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the scanout stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_scanout_plan_reset (static)
 *   - login_window_credential_screen_scanout_plan_build
 *
 * The scanout-plan converts a fail-closed vsync-plan into a
 * scanout contract for the downstream display stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_scanout_plan_reset(
    struct login_window_credential_screen_scanout_plan *out,
    int vsync_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SCANOUT_PLAN_VERSION;
  out->vsync_plan_available = vsync_plan_available ? 1 : 0;
  out->vsync_plan_safe = 0;
  out->scanout_plan_safe = 0;
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
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->scanout_credential_panel = 0;
  out->scanout_credential_input = 0;
  out->scanout_credential_focus = 0;
  out->scanout_text_recovery = 0;
  out->scanout_text_login = 1;
  out->scanout_text_login_resume = 0;
  out->scanout_text_login_fallback = 1;
  out->scanout_error = 1;
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
  out->event_type = "credential-screen-scanout-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "vsync-plan-unavailable";
}

int login_window_credential_screen_scanout_plan_build(
    const struct login_window_credential_screen_vsync_plan *vsync_plan,
    struct login_window_credential_screen_scanout_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_scanout_plan_reset(out, vsync_plan ? 1 : 0);
  if (!vsync_plan) return 0;
  out->requested_action = vsync_plan->requested_action;
  out->vsync_plan_safe = vsync_plan->vsync_plan_safe ? 1 : 0;
  safe = vsync_plan->vsync_plan_safe && vsync_plan->vsync_required &&
         vsync_plan->vsync_allowed && !vsync_plan->vsync_submitted &&
         vsync_plan->vsync_ticket_selected && !vsync_plan->vsync_error &&
         !vsync_plan->vsync_wait_allowed && !vsync_plan->vsync_wait_submitted &&
         vsync_plan->vsync_fence_required && !vsync_plan->vsync_fence_armed &&
         vsync_plan->schedule_required && vsync_plan->schedule_allowed &&
         !vsync_plan->schedule_submitted && vsync_plan->schedule_ticket_selected &&
         vsync_plan->present_required && vsync_plan->present_allowed &&
         !vsync_plan->present_submitted && vsync_plan->present_ticket_selected &&
         vsync_plan->damage_required && vsync_plan->damage_allowed &&
         !vsync_plan->damage_submitted && vsync_plan->compositor_surface_allowed &&
         !vsync_plan->compositor_surface_submitted &&
         vsync_plan->compositor_damage_planned &&
         vsync_plan->compositor_damage_allowed &&
         !vsync_plan->compositor_damage_submitted &&
         vsync_plan->frame_pacing_required && vsync_plan->frame_pacing_allowed &&
         !vsync_plan->frame_timer_armed && !vsync_plan->compositor_wake_allowed &&
         !vsync_plan->compositor_wake_submitted && !vsync_plan->page_flip_allowed &&
         !vsync_plan->page_flip_submitted && vsync_plan->route_selected &&
         !vsync_plan->route_blocked && vsync_plan->credential_session_safe &&
         vsync_plan->credential_storage_wiped && vsync_plan->credential_redacted &&
         vsync_plan->length_redacted && !vsync_plan->raw_secret_exposed &&
         !vsync_plan->masked_text_exposed && vsync_plan->submit_blocked &&
         !vsync_plan->submit_enabled && !vsync_plan->auth_attempt_allowed &&
         !vsync_plan->submit_callback_bound && !vsync_plan->auth_callback_bound &&
         vsync_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-scanout-plan-unsafe";
    out->blocked_reason = "credential-scanout-plan-unsafe";
    out->message = "Credential screen scanout plan unsafe; use text login.";
    return 0;
  }
  out->scanout_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = vsync_plan->action_allowed ? 1 : 0;
  out->action_blocked = vsync_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = vsync_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = vsync_plan->damage_required ? 1 : 0;
  out->damage_allowed = vsync_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = vsync_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = vsync_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = vsync_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = vsync_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = vsync_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = vsync_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = vsync_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = vsync_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = vsync_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = vsync_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = vsync_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = vsync_plan->schedule_reuse_allowed ? 1 : 0;
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
  out->display_mode_required = 1;
  out->display_mode_committed = 0;
  out->scanout_error = 0;
  out->recovery_text_session_required = vsync_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = vsync_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = vsync_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = vsync_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = vsync_plan->view;
  out->widget_tree = vsync_plan->widget_tree;
  out->damage_ticket = vsync_plan->damage_ticket;
  out->present_ticket = vsync_plan->present_ticket;
  out->schedule_ticket = vsync_plan->schedule_ticket;
  out->vsync_ticket = vsync_plan->vsync_ticket;
  out->focus_target = vsync_plan->focus_target;
  out->primary_action = vsync_plan->primary_action;
  out->route = vsync_plan->route;
  out->compositor_target = vsync_plan->compositor_target;
  out->damage_policy = vsync_plan->damage_policy;
  out->cache_policy = vsync_plan->cache_policy;
  out->present_policy = vsync_plan->present_policy;
  out->schedule_policy = vsync_plan->schedule_policy;
  out->vsync_policy = vsync_plan->vsync_policy;
  out->scanout_policy = out->schedule_incremental_allowed ?
      "incremental-scanout-declarative" : "full-scanout-declarative";
  out->event_type = "credential-screen-scanout-plan-ready";
  out->state = "scanout-ready";
  out->message = "Credential screen scanout ticket ready; no display commit performed.";
  out->blocked_reason = vsync_plan->blocked_reason;
  if (vsync_plan->submit_requested) {
    out->scanout_ticket = "text-login-fallback-scanout-ticket";
    out->compositor_target = "text-login-fallback-scanout";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->scanout_text_login = 1;
    out->scanout_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "scanout-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (vsync_plan->vsync_credential_panel && vsync_plan->vsync_credential_input &&
      vsync_plan->vsync_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->scanout_ticket = "credential-screen-scanout-ticket";
    out->compositor_target = "credential-screen-scanout";
    out->scanout_credential_panel = 1;
    out->scanout_credential_input = 1;
    out->scanout_credential_focus = 1;
    out->scanout_text_login = 0;
    out->scanout_text_login_fallback = 0;
    out->state = "scanout-credential-ready";
    out->message = "Credential scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (vsync_plan->vsync_text_recovery && out->recovery_text_session_required) {
    out->scanout_ticket = "text-recovery-scanout-ticket";
    out->compositor_target = "text-recovery-scanout";
    out->scanout_text_recovery = 1;
    out->scanout_text_login = 1;
    out->scanout_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "scanout-text-recovery-ready";
    out->message = "Text recovery scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (vsync_plan->vsync_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->scanout_ticket = "text-login-resume-scanout-ticket";
    out->compositor_target = "text-login-resume-scanout";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->vsync_policy = "full-vsync-declarative";
    out->scanout_policy = "full-scanout-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->scanout_text_login = 1;
    out->scanout_text_login_resume = 1;
    out->scanout_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "scanout-resume-ready";
    out->message = "Text login resume scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->scanout_ticket = "text-login-fallback-scanout-ticket";
  out->compositor_target = "text-login-fallback-scanout";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->scanout_text_login = 1;
  out->scanout_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "scanout-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
