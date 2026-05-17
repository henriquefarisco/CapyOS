/*
 * src/auth/login_runtime/window_compositor_plan.c
 *
 * Credential-screen window-compositor plan reset + safety predicate
 * + copy helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.50 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * window-compositor stage and the public builder that consumes
 * them:
 *
 *   - login_window_credential_screen_window_compositor_plan_reset (static)
 *   - login_window_credential_screen_window_compositor_plan_surface_is_safe (static)
 *   - login_window_credential_screen_window_compositor_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_window_compositor_plan_build
 *
 * The window-compositor-plan converts a fail-closed window-surface-
 * plan into a window-compositor contract for the downstream
 * window-damage stage.  The static `_surface_is_safe` predicate
 * consolidates the upstream-safety check and `_copy_safe_fields`
 * performs the field-by-field propagation used when the upstream
 * contract is safe.  All helpers are kept file-local so they cannot
 * be reused outside this translation unit; the public builder is
 * the only entry point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_window_compositor_plan_reset(
    struct login_window_credential_screen_window_compositor_plan *out,
    int window_surface_plan_available) {
  *out = (struct login_window_credential_screen_window_compositor_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_COMPOSITOR_PLAN_VERSION;
  out->window_surface_plan_available = window_surface_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->release_required = 1;
  out->gui_required = 1;
  out->window_required = 1;
  out->surface_required = 1;
  out->compositor_required = 1;
  out->compositor_text_login = 1;
  out->compositor_text_login_fallback = 1;
  out->compositor_error = 1;
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
  out->event_type = "credential-screen-window-compositor-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-surface-plan-unavailable";
}

static int
login_window_credential_screen_window_compositor_plan_surface_is_safe(
    const struct login_window_credential_screen_window_surface_plan *surface_plan) {
  return surface_plan->window_surface_plan_safe &&
         surface_plan->window_plan_available &&
         surface_plan->window_plan_safe &&
         surface_plan->compaction_required &&
         surface_plan->compaction_allowed &&
         !surface_plan->compaction_submitted &&
         surface_plan->compaction_ticket_selected &&
         surface_plan->compaction_target_selected &&
         !surface_plan->compaction_storage_write_allowed &&
         !surface_plan->compaction_storage_written &&
         !surface_plan->compaction_resource_release_allowed &&
         !surface_plan->compaction_resource_released &&
         !surface_plan->compaction_cpu_gpu_sync_allowed &&
         !surface_plan->compaction_cpu_gpu_sync_submitted &&
         surface_plan->reclaim_required &&
         surface_plan->reclaim_allowed &&
         !surface_plan->reclaim_submitted &&
         surface_plan->reclaim_ticket_selected &&
         surface_plan->reclaim_target_selected &&
         !surface_plan->reclaim_storage_prune_allowed &&
         !surface_plan->reclaim_storage_pruned &&
         !surface_plan->reclaim_resource_release_allowed &&
         !surface_plan->reclaim_resource_released &&
         !surface_plan->reclaim_cpu_gpu_sync_allowed &&
         !surface_plan->reclaim_cpu_gpu_sync_submitted &&
         surface_plan->release_required &&
         surface_plan->release_allowed &&
         !surface_plan->release_submitted &&
         surface_plan->release_ticket_selected &&
         surface_plan->release_target_selected &&
         !surface_plan->release_storage_prune_allowed &&
         !surface_plan->release_storage_pruned &&
         !surface_plan->release_resource_release_allowed &&
         !surface_plan->release_resource_released &&
         !surface_plan->release_cpu_gpu_sync_allowed &&
         !surface_plan->release_cpu_gpu_sync_submitted &&
         surface_plan->gui_required &&
         surface_plan->gui_allowed &&
         !surface_plan->gui_submitted &&
         surface_plan->gui_ticket_selected &&
         surface_plan->gui_target_selected &&
         !surface_plan->gui_pixels_write_allowed &&
         !surface_plan->gui_pixels_written &&
         !surface_plan->gui_auth_submit_allowed &&
         !surface_plan->gui_auth_attempt_allowed &&
         surface_plan->window_required &&
         surface_plan->window_allowed &&
         !surface_plan->window_created &&
         surface_plan->window_ticket_selected &&
         surface_plan->window_target_selected &&
         !surface_plan->window_surface_bound &&
         !surface_plan->window_input_bound &&
         !surface_plan->window_auth_submit_allowed &&
         !surface_plan->window_auth_attempt_allowed &&
         surface_plan->surface_required &&
         surface_plan->surface_allowed &&
         !surface_plan->surface_bound &&
         surface_plan->surface_ticket_selected &&
         surface_plan->surface_target_selected &&
         !surface_plan->surface_memory_mapped &&
         !surface_plan->surface_pixels_written &&
         !surface_plan->surface_compositor_submit_allowed &&
         !surface_plan->surface_compositor_submitted &&
         !surface_plan->surface_auth_submit_allowed &&
         !surface_plan->surface_auth_attempt_allowed &&
         !surface_plan->surface_error &&
         surface_plan->route_selected &&
         !surface_plan->route_blocked &&
         surface_plan->credential_session_safe &&
         surface_plan->credential_storage_wiped &&
         surface_plan->credential_redacted &&
         surface_plan->length_redacted &&
         !surface_plan->raw_secret_exposed &&
         !surface_plan->masked_text_exposed &&
         surface_plan->submit_blocked &&
         !surface_plan->submit_enabled &&
         !surface_plan->auth_attempt_allowed &&
         !surface_plan->submit_callback_bound &&
         !surface_plan->auth_callback_bound &&
         surface_plan->text_login_authoritative;
}

static void
login_window_credential_screen_window_compositor_plan_copy_safe_fields(
    const struct login_window_credential_screen_window_surface_plan *surface_plan,
    struct login_window_credential_screen_window_compositor_plan *out) {
  out->compaction_required = surface_plan->compaction_required ? 1 : 0;
  out->compaction_allowed = surface_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      surface_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      surface_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_required = surface_plan->reclaim_required ? 1 : 0;
  out->reclaim_allowed = surface_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected =
      surface_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected =
      surface_plan->reclaim_target_selected ? 1 : 0;
  out->release_required = surface_plan->release_required ? 1 : 0;
  out->release_allowed = surface_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected =
      surface_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected =
      surface_plan->release_target_selected ? 1 : 0;
  out->gui_required = surface_plan->gui_required ? 1 : 0;
  out->gui_allowed = surface_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = surface_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = surface_plan->gui_target_selected ? 1 : 0;
  out->window_required = surface_plan->window_required ? 1 : 0;
  out->window_allowed = surface_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected =
      surface_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected =
      surface_plan->window_target_selected ? 1 : 0;
  out->surface_required = surface_plan->surface_required ? 1 : 0;
  out->surface_allowed = surface_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected =
      surface_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected =
      surface_plan->surface_target_selected ? 1 : 0;
  out->recovery_text_session_required =
      surface_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = surface_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      surface_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = surface_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = surface_plan->view;
  out->widget_tree = surface_plan->widget_tree;
  out->compaction_ticket = surface_plan->compaction_ticket;
  out->reclaim_ticket = surface_plan->reclaim_ticket;
  out->release_ticket = surface_plan->release_ticket;
  out->gui_ticket = surface_plan->gui_ticket;
  out->window_ticket = surface_plan->window_ticket;
  out->surface_ticket = surface_plan->surface_ticket;
  out->focus_target = surface_plan->focus_target;
  out->primary_action = surface_plan->primary_action;
  out->route = surface_plan->route;
  out->compaction_policy = surface_plan->compaction_policy;
  out->reclaim_policy = surface_plan->reclaim_policy;
  out->release_policy = surface_plan->release_policy;
  out->gui_policy = surface_plan->gui_policy;
  out->window_policy = surface_plan->window_policy;
  out->surface_policy = surface_plan->surface_policy;
}

int login_window_credential_screen_window_compositor_plan_build(
    const struct login_window_credential_screen_window_surface_plan *surface_plan,
    struct login_window_credential_screen_window_compositor_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_window_compositor_plan_reset(
      out, surface_plan ? 1 : 0);
  if (!surface_plan) return 0;
  out->requested_action = surface_plan->requested_action;
  out->window_surface_plan_safe =
      surface_plan->window_surface_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_window_compositor_plan_surface_is_safe(
          surface_plan)) {
    out->event_type = "credential-screen-window-compositor-plan-unsafe";
    out->blocked_reason = "credential-window-compositor-plan-unsafe";
    out->message =
        "Credential screen window compositor plan unsafe; use text login.";
    return 0;
  }
  out->window_compositor_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = surface_plan->action_allowed ? 1 : 0;
  out->action_blocked = surface_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = surface_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_window_compositor_plan_copy_safe_fields(
      surface_plan, out);
  out->compositor_allowed = 1;
  out->compositor_ticket_selected = 1;
  out->compositor_target_selected = 1;
  out->compositor_surface_allowed = 1;
  out->compositor_damage_planned = 1;
  out->compositor_damage_allowed = 1;
  out->compositor_error = 0;
  out->compositor_policy = "declarative-window-compositor-no-submit";
  out->event_type = "credential-screen-window-compositor-plan-ready";
  out->state = "window-compositor-ready";
  out->message =
      "Credential screen window compositor ticket ready; no compositor submitted.";
  out->blocked_reason = surface_plan->blocked_reason;
  if (surface_plan->submit_requested) {
    out->compositor_ticket = "text-login-fallback-window-compositor-ticket";
    out->compositor_target = "text-login-fallback-window-compositor";
    out->compositor_policy = "fallback-window-compositor-declarative";
    out->compositor_text_login = 1;
    out->compositor_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-compositor-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (surface_plan->surface_credential_panel &&
      surface_plan->surface_credential_input &&
      surface_plan->surface_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->compositor_ticket = "credential-screen-window-compositor-ticket";
    out->compositor_target = "credential-screen-window-compositor";
    out->compositor_credential_panel = 1;
    out->compositor_credential_input = 1;
    out->compositor_credential_focus = 1;
    out->compositor_text_login = 0;
    out->compositor_text_login_fallback = 0;
    out->state = "window-compositor-credential-ready";
    out->message = "Credential window compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (surface_plan->surface_text_recovery &&
      out->recovery_text_session_required) {
    out->compositor_ticket = "text-recovery-window-compositor-ticket";
    out->compositor_target = "text-recovery-window-compositor";
    out->compositor_text_recovery = 1;
    out->compositor_text_login = 1;
    out->compositor_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-compositor-text-recovery-ready";
    out->message = "Text recovery window compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (surface_plan->surface_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->compositor_ticket = "text-login-resume-window-compositor-ticket";
    out->compositor_target = "text-login-resume-window-compositor";
    out->compositor_policy = "full-window-compositor-declarative";
    out->compositor_text_login = 1;
    out->compositor_text_login_resume = 1;
    out->compositor_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "window-compositor-resume-ready";
    out->message = "Text login resume window compositor ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->compositor_ticket = "text-login-fallback-window-compositor-ticket";
  out->compositor_target = "text-login-fallback-window-compositor";
  out->compositor_policy = "fallback-window-compositor-declarative";
  out->compositor_text_login = 1;
  out->compositor_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-compositor-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
