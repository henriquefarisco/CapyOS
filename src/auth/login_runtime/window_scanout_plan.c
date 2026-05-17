/*
 * src/auth/login_runtime/window_scanout_plan.c
 *
 * Credential-screen window-scanout plan reset + builder —
 * extracted byte-for-byte from `src/auth/login_runtime.c` during PR
 * C.55 of the Estagio C dedicated plan.  Hosts the per-plan reset
 * helper and the public builder for the window-scanout stage of
 * the credential pipeline:
 *
 *   - login_window_credential_screen_window_scanout_plan_reset (static)
 *   - login_window_credential_screen_window_scanout_plan_build
 *
 * The window-scanout-plan converts a fail-closed window-vsync-plan
 * into a window-scanout contract for the downstream window-display
 * stage.  The static `_reset` helper is the canonical "blocked"
 * initializer used when the upstream contract is missing or unsafe.
 * Both helpers are kept file-local so they cannot be reused outside
 * this translation unit; the public builder is the only entry
 * point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_window_scanout_plan_reset(
    struct login_window_credential_screen_window_scanout_plan *out,
    int window_vsync_plan_available) {
  *out = (struct login_window_credential_screen_window_scanout_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_SCANOUT_PLAN_VERSION;
  out->window_vsync_plan_available = window_vsync_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->release_required = 1;
  out->gui_required = 1;
  out->window_required = 1;
  out->surface_required = 1;
  out->compositor_required = 1;
  out->damage_required = 1;
  out->full_damage_required = 1;
  out->damage_error = 1;
  out->present_required = 1;
  out->full_present_required = 1;
  out->present_error = 1;
  out->schedule_required = 1;
  out->full_schedule_required = 1;
  out->frame_pacing_required = 1;
  out->schedule_error = 1;
  out->vsync_required = 1;
  out->vsync_fence_required = 1;
  out->vsync_error = 1;
  out->scanout_required = 1;
  out->scanout_text_login = 1;
  out->scanout_text_login_fallback = 1;
  out->scanout_error = 1;
  out->recovery_text_session_required = 1;
  out->session_reset_required = 1;
  out->login_screen_rerender_required = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->compaction_ticket = "text-login-fallback-compaction-ticket";
  out->reclaim_ticket = "text-login-fallback-reclaim-ticket";
  out->release_ticket = "text-login-fallback-release-ticket";
  out->gui_ticket = "text-login-fallback-gui-ticket";
  out->window_ticket = "text-login-fallback-window-ticket";
  out->surface_ticket = "text-login-fallback-window-surface-ticket";
  out->compositor_ticket = "text-login-fallback-window-compositor-ticket";
  out->damage_ticket = "text-login-fallback-window-damage-ticket";
  out->present_ticket = "text-login-fallback-window-present-ticket";
  out->schedule_ticket = "text-login-fallback-window-schedule-ticket";
  out->vsync_ticket = "text-login-fallback-window-vsync-ticket";
  out->scanout_ticket = "text-login-fallback-window-scanout-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->compaction_policy = "compaction-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->release_policy = "release-disabled";
  out->gui_policy = "gui-disabled";
  out->window_policy = "window-disabled";
  out->surface_policy = "surface-disabled";
  out->compositor_policy = "compositor-disabled";
  out->damage_policy = "damage-disabled";
  out->cache_policy = "window-scanout-cache-disabled";
  out->present_policy = "present-disabled";
  out->schedule_policy = "schedule-disabled";
  out->vsync_policy = "vsync-disabled";
  out->scanout_policy = "scanout-disabled";
  out->event_type = "credential-screen-window-scanout-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-vsync-plan-unavailable";
}

int login_window_credential_screen_window_scanout_plan_build(
    const struct login_window_credential_screen_window_vsync_plan *vsync_plan,
    struct login_window_credential_screen_window_scanout_plan *out) {
  int safe = 0;
  if (!out) return -1;
  login_window_credential_screen_window_scanout_plan_reset(
      out, vsync_plan ? 1 : 0);
  if (!vsync_plan) return 0;
  out->requested_action = vsync_plan->requested_action;
  out->window_schedule_plan_available =
      vsync_plan->window_schedule_plan_available ? 1 : 0;
  out->window_present_plan_available =
      vsync_plan->window_present_plan_available ? 1 : 0;
  out->window_damage_plan_available =
      vsync_plan->window_damage_plan_available ? 1 : 0;
  out->window_compositor_plan_available =
      vsync_plan->window_compositor_plan_available ? 1 : 0;
  out->window_surface_plan_available =
      vsync_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      vsync_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      vsync_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      vsync_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      vsync_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      vsync_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      vsync_plan->window_vsync_plan_safe ? 1 : 0;
  safe = vsync_plan->window_vsync_plan_safe &&
         vsync_plan->window_schedule_plan_safe &&
         vsync_plan->window_present_plan_safe &&
         vsync_plan->window_damage_plan_safe &&
         vsync_plan->window_compositor_plan_safe &&
         vsync_plan->window_surface_plan_safe &&
         vsync_plan->window_present_plan_available &&
         vsync_plan->window_damage_plan_available &&
         vsync_plan->window_compositor_plan_available &&
         vsync_plan->window_surface_plan_available &&
         vsync_plan->vsync_required && vsync_plan->vsync_allowed &&
         !vsync_plan->vsync_submitted &&
         vsync_plan->vsync_ticket_selected &&
         vsync_plan->vsync_target_selected &&
         !vsync_plan->vsync_wait_allowed &&
         !vsync_plan->vsync_wait_submitted &&
         vsync_plan->vsync_fence_required &&
         !vsync_plan->vsync_fence_armed && !vsync_plan->vsync_error &&
         vsync_plan->schedule_required && vsync_plan->schedule_allowed &&
         !vsync_plan->schedule_submitted &&
         vsync_plan->schedule_ticket_selected &&
         vsync_plan->schedule_target_selected &&
         !vsync_plan->schedule_cache_hit &&
         !vsync_plan->schedule_auth_submit_allowed &&
         !vsync_plan->schedule_auth_attempt_allowed &&
         !vsync_plan->schedule_error &&
         vsync_plan->frame_pacing_required &&
         vsync_plan->frame_pacing_allowed &&
         !vsync_plan->frame_timer_armed &&
         !vsync_plan->compositor_wake_allowed &&
         !vsync_plan->compositor_wake_submitted &&
         !vsync_plan->page_flip_allowed &&
         !vsync_plan->page_flip_submitted &&
         vsync_plan->present_required && vsync_plan->present_allowed &&
         !vsync_plan->present_submitted &&
         vsync_plan->present_ticket_selected &&
         vsync_plan->present_target_selected &&
         !vsync_plan->present_cache_hit &&
         !vsync_plan->present_auth_submit_allowed &&
         !vsync_plan->present_auth_attempt_allowed &&
         !vsync_plan->present_error && vsync_plan->damage_required &&
         vsync_plan->damage_allowed && !vsync_plan->damage_submitted &&
         vsync_plan->damage_ticket_selected &&
         vsync_plan->damage_target_selected &&
         !vsync_plan->damage_cache_hit &&
         !vsync_plan->damage_auth_submit_allowed &&
         !vsync_plan->damage_auth_attempt_allowed &&
         !vsync_plan->damage_error &&
         vsync_plan->compositor_required &&
         vsync_plan->compositor_allowed &&
         !vsync_plan->compositor_submitted &&
         vsync_plan->compositor_ticket_selected &&
         vsync_plan->compositor_target_selected &&
         vsync_plan->compositor_surface_allowed &&
         !vsync_plan->compositor_surface_submitted &&
         vsync_plan->compositor_damage_planned &&
         vsync_plan->compositor_damage_allowed &&
         !vsync_plan->compositor_damage_submitted &&
         !vsync_plan->compositor_auth_submit_allowed &&
         !vsync_plan->compositor_auth_attempt_allowed &&
         vsync_plan->surface_required && vsync_plan->surface_allowed &&
         !vsync_plan->surface_bound &&
         vsync_plan->surface_ticket_selected &&
         vsync_plan->surface_target_selected &&
         !vsync_plan->surface_memory_mapped &&
         !vsync_plan->surface_pixels_written &&
         !vsync_plan->surface_compositor_submit_allowed &&
         !vsync_plan->surface_compositor_submitted &&
         !vsync_plan->surface_auth_submit_allowed &&
         !vsync_plan->surface_auth_attempt_allowed &&
         vsync_plan->window_required && vsync_plan->window_allowed &&
         !vsync_plan->window_created &&
         vsync_plan->window_ticket_selected &&
         vsync_plan->window_target_selected &&
         !vsync_plan->window_surface_bound &&
         !vsync_plan->window_input_bound &&
         !vsync_plan->window_auth_submit_allowed &&
         !vsync_plan->window_auth_attempt_allowed &&
         vsync_plan->gui_required && vsync_plan->gui_allowed &&
         !vsync_plan->gui_submitted &&
         vsync_plan->gui_ticket_selected &&
         vsync_plan->gui_target_selected &&
         !vsync_plan->gui_pixels_write_allowed &&
         !vsync_plan->gui_pixels_written &&
         !vsync_plan->gui_auth_submit_allowed &&
         !vsync_plan->gui_auth_attempt_allowed &&
         vsync_plan->release_required && vsync_plan->release_allowed &&
         !vsync_plan->release_submitted &&
         vsync_plan->release_ticket_selected &&
         vsync_plan->release_target_selected &&
         !vsync_plan->release_storage_prune_allowed &&
         !vsync_plan->release_storage_pruned &&
         !vsync_plan->release_resource_release_allowed &&
         !vsync_plan->release_resource_released &&
         !vsync_plan->release_cpu_gpu_sync_allowed &&
         !vsync_plan->release_cpu_gpu_sync_submitted &&
         vsync_plan->reclaim_required && vsync_plan->reclaim_allowed &&
         !vsync_plan->reclaim_submitted &&
         vsync_plan->reclaim_ticket_selected &&
         vsync_plan->reclaim_target_selected &&
         !vsync_plan->reclaim_storage_prune_allowed &&
         !vsync_plan->reclaim_storage_pruned &&
         !vsync_plan->reclaim_resource_release_allowed &&
         !vsync_plan->reclaim_resource_released &&
         !vsync_plan->reclaim_cpu_gpu_sync_allowed &&
         !vsync_plan->reclaim_cpu_gpu_sync_submitted &&
         vsync_plan->compaction_required &&
         vsync_plan->compaction_allowed &&
         !vsync_plan->compaction_submitted &&
         vsync_plan->compaction_ticket_selected &&
         vsync_plan->compaction_target_selected &&
         !vsync_plan->compaction_storage_write_allowed &&
         !vsync_plan->compaction_storage_written &&
         !vsync_plan->compaction_resource_release_allowed &&
         !vsync_plan->compaction_resource_released &&
         !vsync_plan->compaction_cpu_gpu_sync_allowed &&
         !vsync_plan->compaction_cpu_gpu_sync_submitted &&
         vsync_plan->route_selected && !vsync_plan->route_blocked &&
         vsync_plan->credential_session_safe &&
         vsync_plan->credential_storage_wiped &&
         vsync_plan->credential_redacted && vsync_plan->length_redacted &&
         !vsync_plan->raw_secret_exposed &&
         !vsync_plan->masked_text_exposed && vsync_plan->submit_blocked &&
         !vsync_plan->submit_enabled && !vsync_plan->auth_attempt_allowed &&
         !vsync_plan->submit_callback_bound &&
         !vsync_plan->auth_callback_bound &&
         vsync_plan->text_login_authoritative;
  if (!safe) {
    out->event_type = "credential-screen-window-scanout-plan-unsafe";
    out->blocked_reason = "credential-window-scanout-plan-unsafe";
    out->message =
        "Credential screen window scanout plan unsafe; use text login.";
    return 0;
  }
  out->window_scanout_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = vsync_plan->action_allowed ? 1 : 0;
  out->action_blocked = vsync_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = vsync_plan->input_focus_allowed ? 1 : 0;
  out->compaction_allowed = vsync_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      vsync_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      vsync_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_allowed = vsync_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected = vsync_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected = vsync_plan->reclaim_target_selected ? 1 : 0;
  out->release_allowed = vsync_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected = vsync_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected = vsync_plan->release_target_selected ? 1 : 0;
  out->gui_allowed = vsync_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = vsync_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = vsync_plan->gui_target_selected ? 1 : 0;
  out->window_allowed = vsync_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected = vsync_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected = vsync_plan->window_target_selected ? 1 : 0;
  out->surface_allowed = vsync_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected = vsync_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected = vsync_plan->surface_target_selected ? 1 : 0;
  out->compositor_allowed = vsync_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      vsync_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      vsync_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      vsync_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      vsync_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      vsync_plan->compositor_damage_allowed ? 1 : 0;
  out->damage_allowed = vsync_plan->damage_allowed ? 1 : 0;
  out->damage_ticket_selected = vsync_plan->damage_ticket_selected ? 1 : 0;
  out->damage_target_selected = vsync_plan->damage_target_selected ? 1 : 0;
  out->damage_incremental_allowed =
      vsync_plan->damage_incremental_allowed ? 1 : 0;
  out->full_damage_required = vsync_plan->full_damage_required ? 1 : 0;
  out->damage_cache_allowed = vsync_plan->damage_cache_allowed ? 1 : 0;
  out->damage_reuse_allowed = vsync_plan->damage_reuse_allowed ? 1 : 0;
  out->present_allowed = vsync_plan->present_allowed ? 1 : 0;
  out->present_ticket_selected = vsync_plan->present_ticket_selected ? 1 : 0;
  out->present_target_selected = vsync_plan->present_target_selected ? 1 : 0;
  out->present_incremental_allowed =
      vsync_plan->present_incremental_allowed ? 1 : 0;
  out->full_present_required = vsync_plan->full_present_required ? 1 : 0;
  out->present_cache_allowed = vsync_plan->present_cache_allowed ? 1 : 0;
  out->present_reuse_allowed = vsync_plan->present_reuse_allowed ? 1 : 0;
  out->schedule_allowed = vsync_plan->schedule_allowed ? 1 : 0;
  out->schedule_ticket_selected =
      vsync_plan->schedule_ticket_selected ? 1 : 0;
  out->schedule_target_selected =
      vsync_plan->schedule_target_selected ? 1 : 0;
  out->schedule_incremental_allowed =
      vsync_plan->schedule_incremental_allowed ? 1 : 0;
  out->full_schedule_required = vsync_plan->full_schedule_required ? 1 : 0;
  out->schedule_cache_allowed = vsync_plan->schedule_cache_allowed ? 1 : 0;
  out->schedule_reuse_allowed = vsync_plan->schedule_reuse_allowed ? 1 : 0;
  out->frame_pacing_allowed = 1;
  out->vsync_allowed = vsync_plan->vsync_allowed ? 1 : 0;
  out->vsync_ticket_selected = vsync_plan->vsync_ticket_selected ? 1 : 0;
  out->vsync_target_selected = vsync_plan->vsync_target_selected ? 1 : 0;
  out->scanout_allowed = 1;
  out->scanout_ticket_selected = 1;
  out->scanout_target_selected = 1;
  out->damage_error = 0;
  out->present_error = 0;
  out->schedule_error = 0;
  out->vsync_error = 0;
  out->scanout_error = 0;
  out->recovery_text_session_required =
      vsync_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = vsync_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      vsync_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = vsync_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = vsync_plan->view;
  out->widget_tree = vsync_plan->widget_tree;
  out->compaction_ticket = vsync_plan->compaction_ticket;
  out->reclaim_ticket = vsync_plan->reclaim_ticket;
  out->release_ticket = vsync_plan->release_ticket;
  out->gui_ticket = vsync_plan->gui_ticket;
  out->window_ticket = vsync_plan->window_ticket;
  out->surface_ticket = vsync_plan->surface_ticket;
  out->compositor_ticket = vsync_plan->compositor_ticket;
  out->damage_ticket = vsync_plan->damage_ticket;
  out->present_ticket = vsync_plan->present_ticket;
  out->schedule_ticket = vsync_plan->schedule_ticket;
  out->vsync_ticket = vsync_plan->vsync_ticket;
  out->focus_target = vsync_plan->focus_target;
  out->primary_action = vsync_plan->primary_action;
  out->route = vsync_plan->route;
  out->compositor_target = vsync_plan->compositor_target;
  out->compaction_policy = vsync_plan->compaction_policy;
  out->reclaim_policy = vsync_plan->reclaim_policy;
  out->release_policy = vsync_plan->release_policy;
  out->gui_policy = vsync_plan->gui_policy;
  out->window_policy = vsync_plan->window_policy;
  out->surface_policy = vsync_plan->surface_policy;
  out->compositor_policy = vsync_plan->compositor_policy;
  out->damage_policy = vsync_plan->damage_policy;
  out->cache_policy = vsync_plan->cache_policy;
  out->present_policy = vsync_plan->present_policy;
  out->schedule_policy = vsync_plan->schedule_policy;
  out->vsync_policy = vsync_plan->vsync_policy;
  out->scanout_policy = out->schedule_incremental_allowed
                           ? "incremental-window-scanout-declarative"
                           : "full-window-scanout-declarative";
  out->event_type = "credential-screen-window-scanout-plan-ready";
  out->state = "window-scanout-ready";
  out->message =
      "Credential screen window scanout ticket ready; no buffer attached or flipped.";
  out->blocked_reason = vsync_plan->blocked_reason;
  if (vsync_plan->submit_requested) {
    out->scanout_ticket = "text-login-fallback-window-scanout-ticket";
    out->compositor_target = "text-login-fallback-window-scanout";
    out->scanout_policy = "fallback-window-scanout-declarative";
    out->scanout_text_login = 1;
    out->scanout_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-scanout-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (vsync_plan->vsync_credential_panel &&
      vsync_plan->vsync_credential_input &&
      vsync_plan->vsync_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->scanout_ticket = "credential-screen-window-scanout-ticket";
    out->compositor_target = "credential-screen-window-scanout";
    out->scanout_credential_panel = 1;
    out->scanout_credential_input = 1;
    out->scanout_credential_focus = 1;
    out->scanout_text_login = 0;
    out->scanout_text_login_fallback = 0;
    out->state = "window-scanout-credential-ready";
    out->message =
        "Credential window scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (vsync_plan->vsync_text_recovery &&
      out->recovery_text_session_required) {
    out->scanout_ticket = "text-recovery-window-scanout-ticket";
    out->compositor_target = "text-recovery-window-scanout";
    out->scanout_text_recovery = 1;
    out->scanout_text_login = 1;
    out->scanout_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-scanout-text-recovery-ready";
    out->message =
        "Text recovery window scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (vsync_plan->vsync_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->scanout_ticket = "text-login-resume-window-scanout-ticket";
    out->compositor_target = "text-login-resume-window-scanout";
    out->scanout_policy = "full-window-scanout-declarative";
    out->cache_policy = "window-scanout-cache-bypassed-for-rerender";
    out->scanout_text_login = 1;
    out->scanout_text_login_resume = 1;
    out->scanout_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-scanout-resume-ready";
    out->message =
        "Text login resume window scanout ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->scanout_ticket = "text-login-fallback-window-scanout-ticket";
  out->compositor_target = "text-login-fallback-window-scanout";
  out->scanout_policy = "fallback-window-scanout-declarative";
  out->scanout_text_login = 1;
  out->scanout_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-scanout-text-login-ready";
  out->message = "Use authoritative text login.";
  out->blocked_reason = "ready";
  return 0;
}
