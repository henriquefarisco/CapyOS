/*
 * src/auth/login_runtime/display_plan.c
 *
 * Credential-screen display plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.19 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the display stage of the credential
 * pipeline:
 *
 *   - login_window_credential_screen_display_plan_reset (static)
 *   - login_window_credential_screen_display_plan_build
 *
 * The display-plan converts a fail-closed scanout-plan into a
 * display contract for the downstream output stage.  The static
 * `_reset` helper is the canonical "blocked" initializer shared
 * between the build path and any future internal callers.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_display_plan_reset(
    struct login_window_credential_screen_display_plan *out,
    int scanout_plan_available) {
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPLAY_PLAN_VERSION;
  out->scanout_plan_available = scanout_plan_available ? 1 : 0;
  out->scanout_plan_safe = 0;
  out->display_plan_safe = 0;
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
  out->display_credential_panel = 0;
  out->display_credential_input = 0;
  out->display_credential_focus = 0;
  out->display_text_recovery = 0;
  out->display_text_login = 1;
  out->display_text_login_resume = 0;
  out->display_text_login_fallback = 1;
  out->display_error = 1;
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
  out->event_type = "credential-screen-display-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "scanout-plan-unavailable";
}

int login_window_credential_screen_display_plan_build(
    const struct login_window_credential_screen_scanout_plan *scanout_plan,
    struct login_window_credential_screen_display_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_display_plan_reset(out, scanout_plan ? 1 : 0);
  if (!scanout_plan) return 0;
  out->requested_action = scanout_plan->requested_action;
  out->scanout_plan_safe = scanout_plan->scanout_plan_safe ? 1 : 0;
  safe = scanout_plan->scanout_plan_safe && scanout_plan->scanout_required &&
         scanout_plan->scanout_allowed && !scanout_plan->scanout_submitted &&
         scanout_plan->scanout_ticket_selected && scanout_plan->scanout_target_selected &&
         !scanout_plan->scanout_buffer_attached && !scanout_plan->scanout_buffer_submitted &&
         scanout_plan->display_mode_required && !scanout_plan->display_mode_committed &&
         !scanout_plan->scanout_error && scanout_plan->vsync_required &&
         scanout_plan->vsync_allowed && !scanout_plan->vsync_submitted &&
         scanout_plan->vsync_ticket_selected && !scanout_plan->vsync_wait_allowed &&
         !scanout_plan->vsync_wait_submitted && scanout_plan->vsync_fence_required &&
         !scanout_plan->vsync_fence_armed && scanout_plan->schedule_required &&
         scanout_plan->schedule_allowed && !scanout_plan->schedule_submitted &&
         scanout_plan->schedule_ticket_selected && scanout_plan->present_required &&
         scanout_plan->present_allowed && !scanout_plan->present_submitted &&
         scanout_plan->present_ticket_selected && scanout_plan->damage_required &&
         scanout_plan->damage_allowed && !scanout_plan->damage_submitted &&
         scanout_plan->compositor_surface_allowed &&
         !scanout_plan->compositor_surface_submitted &&
         scanout_plan->compositor_damage_planned &&
         scanout_plan->compositor_damage_allowed &&
         !scanout_plan->compositor_damage_submitted &&
         scanout_plan->frame_pacing_required && scanout_plan->frame_pacing_allowed &&
         !scanout_plan->frame_timer_armed && !scanout_plan->compositor_wake_allowed &&
         !scanout_plan->compositor_wake_submitted && !scanout_plan->page_flip_allowed &&
         !scanout_plan->page_flip_submitted && scanout_plan->route_selected &&
         !scanout_plan->route_blocked && scanout_plan->credential_session_safe &&
         scanout_plan->credential_storage_wiped && scanout_plan->credential_redacted &&
         scanout_plan->length_redacted && !scanout_plan->raw_secret_exposed &&
         !scanout_plan->masked_text_exposed && scanout_plan->submit_blocked &&
         !scanout_plan->submit_enabled && !scanout_plan->auth_attempt_allowed &&
         !scanout_plan->submit_callback_bound && !scanout_plan->auth_callback_bound &&
         scanout_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-display-plan-unsafe";
    out->blocked_reason = "credential-display-plan-unsafe";
    out->message = "Credential screen display plan unsafe; use text login.";
    return 0;
  }
  out->display_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = scanout_plan->action_allowed ? 1 : 0;
  out->action_blocked = scanout_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = scanout_plan->input_focus_allowed ? 1 : 0;
  out->compositor_surface_allowed = 1;
  out->compositor_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_damage_submitted = 0;
  out->damage_required = scanout_plan->damage_required ? 1 : 0;
  out->damage_allowed = scanout_plan->damage_allowed ? 1 : 0;
  out->damage_incremental_allowed = scanout_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = scanout_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = scanout_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = scanout_plan->damage_reuse_allowed ? 1 : 0;
  out->present_required = 1;
  out->present_allowed = 1;
  out->present_submitted = 0;
  out->present_ticket_selected = 1;
  out->present_incremental_allowed = scanout_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = scanout_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = scanout_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = scanout_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_required = 1;
  out->schedule_allowed = 1;
  out->schedule_submitted = 0;
  out->schedule_ticket_selected = 1;
  out->schedule_incremental_allowed = scanout_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = scanout_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = scanout_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = scanout_plan->schedule_reuse_allowed ? 1 : 0;
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
  out->display_error = 0;
  out->recovery_text_session_required = scanout_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = scanout_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = scanout_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = scanout_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = 1;
  out->view = scanout_plan->view;
  out->widget_tree = scanout_plan->widget_tree;
  out->damage_ticket = scanout_plan->damage_ticket;
  out->present_ticket = scanout_plan->present_ticket;
  out->schedule_ticket = scanout_plan->schedule_ticket;
  out->vsync_ticket = scanout_plan->vsync_ticket;
  out->scanout_ticket = scanout_plan->scanout_ticket;
  out->focus_target = scanout_plan->focus_target;
  out->primary_action = scanout_plan->primary_action;
  out->route = scanout_plan->route;
  out->compositor_target = scanout_plan->compositor_target;
  out->damage_policy = scanout_plan->damage_policy;
  out->cache_policy = scanout_plan->cache_policy;
  out->present_policy = scanout_plan->present_policy;
  out->schedule_policy = scanout_plan->schedule_policy;
  out->vsync_policy = scanout_plan->vsync_policy;
  out->scanout_policy = scanout_plan->scanout_policy;
  out->display_policy = out->schedule_incremental_allowed ?
      "incremental-display-declarative" : "full-display-declarative";
  out->event_type = "credential-screen-display-plan-ready";
  out->state = "display-ready";
  out->message = "Credential screen display ticket ready; no display submission performed.";
  out->blocked_reason = scanout_plan->blocked_reason;
  if (scanout_plan->submit_requested) {
    out->display_ticket = "text-login-fallback-display-ticket";
    out->compositor_target = "text-login-fallback-display";
    out->damage_policy = "fallback-declarative";
    out->present_policy = "fallback-present-declarative";
    out->schedule_policy = "fallback-schedule-declarative";
    out->vsync_policy = "fallback-vsync-declarative";
    out->scanout_policy = "fallback-scanout-declarative";
    out->display_policy = "fallback-display-declarative";
    out->display_text_login = 1;
    out->display_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "display-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (scanout_plan->scanout_credential_panel && scanout_plan->scanout_credential_input &&
      scanout_plan->scanout_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->display_ticket = "credential-screen-display-ticket";
    out->compositor_target = "credential-screen-display";
    out->display_credential_panel = 1;
    out->display_credential_input = 1;
    out->display_credential_focus = 1;
    out->display_text_login = 0;
    out->display_text_login_fallback = 0;
    out->state = "display-credential-ready";
    out->message = "Credential display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (scanout_plan->scanout_text_recovery && out->recovery_text_session_required) {
    out->display_ticket = "text-recovery-display-ticket";
    out->compositor_target = "text-recovery-display";
    out->display_text_recovery = 1;
    out->display_text_login = 1;
    out->display_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "display-text-recovery-ready";
    out->message = "Text recovery display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (scanout_plan->scanout_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->display_ticket = "text-login-resume-display-ticket";
    out->compositor_target = "text-login-resume-display";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->present_policy = "full-present-declarative";
    out->schedule_policy = "full-schedule-declarative";
    out->vsync_policy = "full-vsync-declarative";
    out->scanout_policy = "full-scanout-declarative";
    out->display_policy = "full-display-declarative";
    out->schedule_reuse_allowed = 0;
    out->schedule_cache_allowed = 0;
    out->full_schedule_required = 1;
    out->schedule_incremental_allowed = 0;
    out->display_text_login = 1;
    out->display_text_login_resume = 1;
    out->display_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "display-resume-ready";
    out->message = "Text login resume display ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->display_ticket = "text-login-fallback-display-ticket";
  out->compositor_target = "text-login-fallback-display";
  out->damage_policy = "fallback-declarative";
  out->present_policy = "fallback-present-declarative";
  out->schedule_policy = "fallback-schedule-declarative";
  out->vsync_policy = "fallback-vsync-declarative";
  out->scanout_policy = "fallback-scanout-declarative";
  out->display_policy = "fallback-display-declarative";
  out->display_text_login = 1;
  out->display_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "display-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
