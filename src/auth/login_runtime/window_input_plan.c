/*
 * src/auth/login_runtime/window_input_plan.c
 *
 * Credential-screen window-input plan reset + builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.63 of
 * the Estagio C dedicated plan.  Hosts the per-plan reset helper
 * and the public builder for the window-input stage of the
 * credential pipeline:
 *
 *   - login_window_credential_screen_window_input_plan_reset (static)
 *   - login_window_credential_screen_window_input_plan_build
 *
 * The window-input-plan converts a fail-closed window-event-plan
 * into a window-input contract.  This is the terminal window stage
 * before the pipeline-safety-report aggregates the entire chain.
 * The static `_reset` helper is the canonical "blocked" initializer
 * used when the upstream contract is missing or unsafe.  Both
 * helpers are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 * Closes Phase 6 of the Estagio C dedicated plan.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_window_input_plan_reset(
    struct login_window_credential_screen_window_input_plan *out,
    int window_event_plan_available) {
  *out = (struct login_window_credential_screen_window_input_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_INPUT_PLAN_VERSION;
  out->window_event_plan_available = window_event_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->event_required = 1;
  out->event_error = 1;
  out->input_required = 1;
  out->input_text_login = 1;
  out->input_text_login_fallback = 1;
  out->input_error = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->event_ticket = "text-login-fallback-window-event-ticket";
  out->vblank_ticket = "text-login-fallback-window-vblank-ticket";
  out->flip_ticket = "text-login-fallback-window-flip-ticket";
  out->commit_ticket = "text-login-fallback-window-commit-ticket";
  out->blit_ticket = "text-login-fallback-window-blit-ticket";
  out->output_ticket = "text-login-fallback-window-output-ticket";
  out->display_ticket = "text-login-fallback-window-display-ticket";
  out->scanout_ticket = "text-login-fallback-window-scanout-ticket";
  out->vsync_ticket = "text-login-fallback-window-vsync-ticket";
  out->schedule_ticket = "text-login-fallback-window-schedule-ticket";
  out->present_ticket = "text-login-fallback-window-present-ticket";
  out->damage_ticket = "text-login-fallback-window-damage-ticket";
  out->compositor_ticket = "text-login-fallback-window-compositor-ticket";
  out->surface_ticket = "text-login-fallback-window-surface-ticket";
  out->window_ticket = "text-login-fallback-window-ticket";
  out->gui_ticket = "text-login-fallback-gui-ticket";
  out->release_ticket = "text-login-fallback-release-ticket";
  out->reclaim_ticket = "text-login-fallback-reclaim-ticket";
  out->compaction_ticket = "text-login-fallback-compaction-ticket";
  out->input_ticket = "text-login-fallback-window-input-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->event_policy = "event-disabled";
  out->vblank_policy = "vblank-disabled";
  out->flip_policy = "flip-disabled";
  out->commit_policy = "commit-disabled";
  out->blit_policy = "blit-disabled";
  out->output_policy = "output-disabled";
  out->display_policy = "display-disabled";
  out->scanout_policy = "scanout-disabled";
  out->vsync_policy = "vsync-disabled";
  out->schedule_policy = "schedule-disabled";
  out->present_policy = "present-disabled";
  out->damage_policy = "damage-disabled";
  out->cache_policy = "window-input-cache-disabled";
  out->compositor_policy = "compositor-disabled";
  out->surface_policy = "surface-disabled";
  out->window_policy = "window-disabled";
  out->gui_policy = "gui-disabled";
  out->release_policy = "release-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->compaction_policy = "compaction-disabled";
  out->input_policy = "input-disabled";
  out->event_type = "credential-screen-window-input-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-event-plan-unavailable";
}

int login_window_credential_screen_window_input_plan_build(
    const struct login_window_credential_screen_window_event_plan *event_plan,
    struct login_window_credential_screen_window_input_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_input_plan_reset(
      out, event_plan ? 1 : 0);
  if (!event_plan) return 0;
  out->requested_action = event_plan->requested_action;
  out->window_vblank_plan_available =
      event_plan->window_vblank_plan_available ? 1 : 0;
  out->window_flip_plan_available =
      event_plan->window_flip_plan_available ? 1 : 0;
  out->window_commit_plan_available =
      event_plan->window_commit_plan_available ? 1 : 0;
  out->window_blit_plan_available =
      event_plan->window_blit_plan_available ? 1 : 0;
  out->window_output_plan_available =
      event_plan->window_output_plan_available ? 1 : 0;
  out->window_display_plan_available =
      event_plan->window_display_plan_available ? 1 : 0;
  out->window_scanout_plan_available =
      event_plan->window_scanout_plan_available ? 1 : 0;
  out->window_vsync_plan_available =
      event_plan->window_vsync_plan_available ? 1 : 0;
  out->window_schedule_plan_available =
      event_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      event_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      event_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      event_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      event_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      event_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      event_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      event_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      event_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      event_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      event_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      event_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      event_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      event_plan->window_output_plan_safe ? 1 : 0;
  out->window_blit_plan_safe =
      event_plan->window_blit_plan_safe ? 1 : 0;
  out->window_commit_plan_safe =
      event_plan->window_commit_plan_safe ? 1 : 0;
  out->window_flip_plan_safe =
      event_plan->window_flip_plan_safe ? 1 : 0;
  out->window_vblank_plan_safe =
      event_plan->window_vblank_plan_safe ? 1 : 0;
  out->window_event_plan_safe =
      event_plan->window_event_plan_safe ? 1 : 0;
  safe = event_plan->window_event_plan_safe &&
         event_plan->window_vblank_plan_safe &&
         event_plan->window_flip_plan_safe &&
         event_plan->window_commit_plan_safe &&
         event_plan->window_blit_plan_safe &&
         event_plan->window_output_plan_safe &&
         event_plan->window_display_plan_safe &&
         event_plan->window_scanout_plan_safe &&
         event_plan->window_vsync_plan_safe &&
         event_plan->window_schedule_plan_safe &&
         event_plan->window_present_plan_safe &&
         event_plan->window_damage_plan_safe &&
         event_plan->window_compositor_plan_safe &&
         event_plan->window_surface_plan_safe &&
         event_plan->window_vblank_plan_available &&
         event_plan->window_flip_plan_available &&
         event_plan->window_commit_plan_available &&
         event_plan->window_blit_plan_available &&
         event_plan->window_output_plan_available &&
         event_plan->window_display_plan_available &&
         event_plan->window_scanout_plan_available &&
         event_plan->window_vsync_plan_available &&
         event_plan->window_schedule_plan_available &&
         event_plan->window_present_plan_available &&
         event_plan->window_damage_plan_available &&
         event_plan->window_compositor_plan_available &&
         event_plan->window_surface_plan_available &&
         event_plan->event_required && event_plan->event_allowed &&
         !event_plan->event_submitted &&
         event_plan->event_ticket_selected &&
         event_plan->event_target_selected &&
         !event_plan->event_handler_armed &&
         !event_plan->event_handler_submitted &&
         !event_plan->event_queue_armed &&
         !event_plan->event_queue_submitted &&
         !event_plan->event_dispatch_allowed &&
         !event_plan->event_dispatch_submitted &&
         !event_plan->event_callback_armed &&
         !event_plan->event_callback_submitted &&
         !event_plan->event_timestamp_captured &&
         !event_plan->event_timestamp_submitted &&
         !event_plan->event_frame_completed &&
         !event_plan->event_frame_submitted &&
         !event_plan->event_error &&
         event_plan->route_selected && !event_plan->route_blocked &&
         event_plan->credential_session_safe &&
         event_plan->credential_storage_wiped &&
         event_plan->credential_redacted &&
         event_plan->length_redacted &&
         !event_plan->raw_secret_exposed &&
         !event_plan->masked_text_exposed &&
         event_plan->submit_blocked && !event_plan->submit_enabled &&
         !event_plan->auth_attempt_allowed &&
         !event_plan->submit_callback_bound &&
         !event_plan->auth_callback_bound &&
         event_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-input-plan-unsafe";
    out->blocked_reason = "credential-window-input-plan-unsafe";
    out->message =
        "Credential screen window input plan unsafe; use text login.";
    return 0;
  }
  out->window_input_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = event_plan->action_allowed ? 1 : 0;
  out->action_blocked = event_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = event_plan->input_focus_allowed ? 1 : 0;
  out->event_allowed = event_plan->event_allowed ? 1 : 0;
  out->event_ticket_selected = event_plan->event_ticket_selected ? 1 : 0;
  out->event_target_selected = event_plan->event_target_selected ? 1 : 0;
  out->event_error = 0;
  out->input_allowed = 1;
  out->input_ticket_selected = 1;
  out->input_target_selected = 1;
  out->input_error = 0;
  out->recovery_text_session_required =
      event_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = event_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      event_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = event_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = event_plan->view;
  out->widget_tree = event_plan->widget_tree;
  out->event_ticket = event_plan->event_ticket;
  out->vblank_ticket = event_plan->vblank_ticket;
  out->flip_ticket = event_plan->flip_ticket;
  out->commit_ticket = event_plan->commit_ticket;
  out->blit_ticket = event_plan->blit_ticket;
  out->output_ticket = event_plan->output_ticket;
  out->display_ticket = event_plan->display_ticket;
  out->scanout_ticket = event_plan->scanout_ticket;
  out->vsync_ticket = event_plan->vsync_ticket;
  out->schedule_ticket = event_plan->schedule_ticket;
  out->present_ticket = event_plan->present_ticket;
  out->damage_ticket = event_plan->damage_ticket;
  out->compositor_ticket = event_plan->compositor_ticket;
  out->surface_ticket = event_plan->surface_ticket;
  out->window_ticket = event_plan->window_ticket;
  out->gui_ticket = event_plan->gui_ticket;
  out->release_ticket = event_plan->release_ticket;
  out->reclaim_ticket = event_plan->reclaim_ticket;
  out->compaction_ticket = event_plan->compaction_ticket;
  out->focus_target = event_plan->focus_target;
  out->primary_action = event_plan->primary_action;
  out->route = event_plan->route;
  out->compositor_target = event_plan->compositor_target;
  out->event_policy = event_plan->event_policy;
  out->vblank_policy = event_plan->vblank_policy;
  out->flip_policy = event_plan->flip_policy;
  out->commit_policy = event_plan->commit_policy;
  out->blit_policy = event_plan->blit_policy;
  out->output_policy = event_plan->output_policy;
  out->display_policy = event_plan->display_policy;
  out->scanout_policy = event_plan->scanout_policy;
  out->vsync_policy = event_plan->vsync_policy;
  out->schedule_policy = event_plan->schedule_policy;
  out->present_policy = event_plan->present_policy;
  out->damage_policy = event_plan->damage_policy;
  out->cache_policy = event_plan->cache_policy;
  out->compositor_policy = event_plan->compositor_policy;
  out->surface_policy = event_plan->surface_policy;
  out->window_policy = event_plan->window_policy;
  out->gui_policy = event_plan->gui_policy;
  out->release_policy = event_plan->release_policy;
  out->reclaim_policy = event_plan->reclaim_policy;
  out->compaction_policy = event_plan->compaction_policy;
  out->input_policy = event_plan->schedule_incremental_allowed
                          ? "incremental-window-input-declarative"
                          : "full-window-input-declarative";
  out->event_type = "credential-screen-window-input-plan-ready";
  out->state = "window-input-ready";
  out->message =
      "Credential screen window input ticket ready; no keyboard, pointer, focus, keymap, decode, route, callback or grab armed.";
  out->blocked_reason = event_plan->blocked_reason;
  if (event_plan->submit_requested) {
    out->input_ticket = "text-login-fallback-window-input-ticket";
    out->compositor_target = "text-login-fallback-window-input";
    out->input_policy = "fallback-window-input-declarative";
    out->input_text_login = 1;
    out->input_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-input-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (event_plan->event_credential_panel &&
      event_plan->event_credential_input &&
      event_plan->event_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->input_ticket = "credential-screen-window-input-ticket";
    out->compositor_target = "credential-screen-window-input";
    out->input_credential_panel = 1;
    out->input_credential_input = 1;
    out->input_credential_focus = 1;
    out->input_text_login = 0;
    out->input_text_login_fallback = 0;
    out->state = "window-input-credential-ready";
    out->message =
        "Credential window input ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (event_plan->event_text_recovery &&
      out->recovery_text_session_required) {
    out->input_ticket = "text-recovery-window-input-ticket";
    out->compositor_target = "text-recovery-window-input";
    out->input_text_recovery = 1;
    out->input_text_login = 1;
    out->input_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-input-text-recovery-ready";
    out->message =
        "Text recovery window input ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (event_plan->event_text_login_resume &&
      out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->input_ticket = "text-login-resume-window-input-ticket";
    out->compositor_target = "text-login-resume-window-input";
    out->input_policy = "full-window-input-declarative";
    out->cache_policy = "window-input-cache-bypassed-for-rerender";
    out->input_text_login = 1;
    out->input_text_login_resume = 1;
    out->input_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-input-resume-ready";
    out->message =
        "Text login resume window input ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->input_ticket = "text-login-fallback-window-input-ticket";
  out->compositor_target = "text-login-fallback-window-input";
  out->input_policy = "fallback-window-input-declarative";
  out->input_text_login = 1;
  out->input_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-input-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
