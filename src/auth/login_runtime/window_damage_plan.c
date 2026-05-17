/*
 * src/auth/login_runtime/window_damage_plan.c
 *
 * Credential-screen window-damage plan reset + safety predicate +
 * copy helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.51 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * window-damage stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_window_damage_plan_reset (static)
 *   - login_window_credential_screen_window_damage_plan_compositor_is_safe (static)
 *   - login_window_credential_screen_window_damage_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_window_damage_plan_build
 *
 * The window-damage-plan converts a fail-closed window-compositor-
 * plan into a window-damage contract for the downstream window-
 * present stage.  The static `_compositor_is_safe` predicate
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

static void login_window_credential_screen_window_damage_plan_reset(
    struct login_window_credential_screen_window_damage_plan *out,
    int window_compositor_plan_available) {
  *out = (struct login_window_credential_screen_window_damage_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_DAMAGE_PLAN_VERSION;
  out->window_compositor_plan_available =
      window_compositor_plan_available ? 1 : 0;
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
  out->damage_text_login = 1;
  out->damage_text_login_fallback = 1;
  out->damage_error = 1;
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
  out->cache_policy = "window-damage-cache-disabled";
  out->event_type = "credential-screen-window-damage-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "window-compositor-plan-unavailable";
}

static int
login_window_credential_screen_window_damage_plan_compositor_is_safe(
    const struct login_window_credential_screen_window_compositor_plan
        *compositor_plan) {
  return compositor_plan->window_surface_plan_available &&
         compositor_plan->window_surface_plan_safe &&
         compositor_plan->window_compositor_plan_safe &&
         compositor_plan->compaction_required &&
         compositor_plan->compaction_allowed &&
         !compositor_plan->compaction_submitted &&
         compositor_plan->compaction_ticket_selected &&
         compositor_plan->compaction_target_selected &&
         !compositor_plan->compaction_storage_write_allowed &&
         !compositor_plan->compaction_storage_written &&
         !compositor_plan->compaction_resource_release_allowed &&
         !compositor_plan->compaction_resource_released &&
         !compositor_plan->compaction_cpu_gpu_sync_allowed &&
         !compositor_plan->compaction_cpu_gpu_sync_submitted &&
         compositor_plan->reclaim_required &&
         compositor_plan->reclaim_allowed &&
         !compositor_plan->reclaim_submitted &&
         compositor_plan->reclaim_ticket_selected &&
         compositor_plan->reclaim_target_selected &&
         !compositor_plan->reclaim_storage_prune_allowed &&
         !compositor_plan->reclaim_storage_pruned &&
         !compositor_plan->reclaim_resource_release_allowed &&
         !compositor_plan->reclaim_resource_released &&
         !compositor_plan->reclaim_cpu_gpu_sync_allowed &&
         !compositor_plan->reclaim_cpu_gpu_sync_submitted &&
         compositor_plan->release_required &&
         compositor_plan->release_allowed &&
         !compositor_plan->release_submitted &&
         compositor_plan->release_ticket_selected &&
         compositor_plan->release_target_selected &&
         !compositor_plan->release_storage_prune_allowed &&
         !compositor_plan->release_storage_pruned &&
         !compositor_plan->release_resource_release_allowed &&
         !compositor_plan->release_resource_released &&
         !compositor_plan->release_cpu_gpu_sync_allowed &&
         !compositor_plan->release_cpu_gpu_sync_submitted &&
         compositor_plan->gui_required &&
         compositor_plan->gui_allowed &&
         !compositor_plan->gui_submitted &&
         compositor_plan->gui_ticket_selected &&
         compositor_plan->gui_target_selected &&
         !compositor_plan->gui_pixels_write_allowed &&
         !compositor_plan->gui_pixels_written &&
         !compositor_plan->gui_auth_submit_allowed &&
         !compositor_plan->gui_auth_attempt_allowed &&
         compositor_plan->window_required &&
         compositor_plan->window_allowed &&
         !compositor_plan->window_created &&
         compositor_plan->window_ticket_selected &&
         compositor_plan->window_target_selected &&
         !compositor_plan->window_surface_bound &&
         !compositor_plan->window_input_bound &&
         !compositor_plan->window_auth_submit_allowed &&
         !compositor_plan->window_auth_attempt_allowed &&
         compositor_plan->surface_required &&
         compositor_plan->surface_allowed &&
         !compositor_plan->surface_bound &&
         compositor_plan->surface_ticket_selected &&
         compositor_plan->surface_target_selected &&
         !compositor_plan->surface_memory_mapped &&
         !compositor_plan->surface_pixels_written &&
         !compositor_plan->surface_compositor_submit_allowed &&
         !compositor_plan->surface_compositor_submitted &&
         !compositor_plan->surface_auth_submit_allowed &&
         !compositor_plan->surface_auth_attempt_allowed &&
         compositor_plan->compositor_required &&
         compositor_plan->compositor_allowed &&
         !compositor_plan->compositor_submitted &&
         compositor_plan->compositor_ticket_selected &&
         compositor_plan->compositor_target_selected &&
         compositor_plan->compositor_surface_allowed &&
         !compositor_plan->compositor_surface_submitted &&
         compositor_plan->compositor_damage_planned &&
         compositor_plan->compositor_damage_allowed &&
         !compositor_plan->compositor_damage_submitted &&
         !compositor_plan->compositor_auth_submit_allowed &&
         !compositor_plan->compositor_auth_attempt_allowed &&
         !compositor_plan->compositor_error &&
         compositor_plan->route_selected &&
         !compositor_plan->route_blocked &&
         compositor_plan->credential_session_safe &&
         compositor_plan->credential_storage_wiped &&
         compositor_plan->credential_redacted &&
         compositor_plan->length_redacted &&
         !compositor_plan->raw_secret_exposed &&
         !compositor_plan->masked_text_exposed &&
         compositor_plan->submit_blocked &&
         !compositor_plan->submit_enabled &&
         !compositor_plan->auth_attempt_allowed &&
         !compositor_plan->submit_callback_bound &&
         !compositor_plan->auth_callback_bound &&
         compositor_plan->text_login_authoritative;
}

static void
login_window_credential_screen_window_damage_plan_copy_safe_fields(
    const struct login_window_credential_screen_window_compositor_plan
        *compositor_plan,
    struct login_window_credential_screen_window_damage_plan *out) {
  out->window_surface_plan_available =
      compositor_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      compositor_plan->window_surface_plan_safe ? 1 : 0;
  out->compaction_required = compositor_plan->compaction_required ? 1 : 0;
  out->compaction_allowed = compositor_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      compositor_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      compositor_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_required = compositor_plan->reclaim_required ? 1 : 0;
  out->reclaim_allowed = compositor_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected =
      compositor_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected =
      compositor_plan->reclaim_target_selected ? 1 : 0;
  out->release_required = compositor_plan->release_required ? 1 : 0;
  out->release_allowed = compositor_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected =
      compositor_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected =
      compositor_plan->release_target_selected ? 1 : 0;
  out->gui_required = compositor_plan->gui_required ? 1 : 0;
  out->gui_allowed = compositor_plan->gui_allowed ? 1 : 0;
  out->gui_ticket_selected = compositor_plan->gui_ticket_selected ? 1 : 0;
  out->gui_target_selected = compositor_plan->gui_target_selected ? 1 : 0;
  out->window_required = compositor_plan->window_required ? 1 : 0;
  out->window_allowed = compositor_plan->window_allowed ? 1 : 0;
  out->window_ticket_selected =
      compositor_plan->window_ticket_selected ? 1 : 0;
  out->window_target_selected =
      compositor_plan->window_target_selected ? 1 : 0;
  out->surface_required = compositor_plan->surface_required ? 1 : 0;
  out->surface_allowed = compositor_plan->surface_allowed ? 1 : 0;
  out->surface_ticket_selected =
      compositor_plan->surface_ticket_selected ? 1 : 0;
  out->surface_target_selected =
      compositor_plan->surface_target_selected ? 1 : 0;
  out->compositor_required = compositor_plan->compositor_required ? 1 : 0;
  out->compositor_allowed = compositor_plan->compositor_allowed ? 1 : 0;
  out->compositor_ticket_selected =
      compositor_plan->compositor_ticket_selected ? 1 : 0;
  out->compositor_target_selected =
      compositor_plan->compositor_target_selected ? 1 : 0;
  out->compositor_surface_allowed =
      compositor_plan->compositor_surface_allowed ? 1 : 0;
  out->compositor_damage_planned =
      compositor_plan->compositor_damage_planned ? 1 : 0;
  out->compositor_damage_allowed =
      compositor_plan->compositor_damage_allowed ? 1 : 0;
  out->recovery_text_session_required =
      compositor_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required =
      compositor_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      compositor_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = compositor_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = compositor_plan->view;
  out->widget_tree = compositor_plan->widget_tree;
  out->compaction_ticket = compositor_plan->compaction_ticket;
  out->reclaim_ticket = compositor_plan->reclaim_ticket;
  out->release_ticket = compositor_plan->release_ticket;
  out->gui_ticket = compositor_plan->gui_ticket;
  out->window_ticket = compositor_plan->window_ticket;
  out->surface_ticket = compositor_plan->surface_ticket;
  out->compositor_ticket = compositor_plan->compositor_ticket;
  out->focus_target = compositor_plan->focus_target;
  out->primary_action = compositor_plan->primary_action;
  out->route = compositor_plan->route;
  out->compaction_policy = compositor_plan->compaction_policy;
  out->reclaim_policy = compositor_plan->reclaim_policy;
  out->release_policy = compositor_plan->release_policy;
  out->gui_policy = compositor_plan->gui_policy;
  out->window_policy = compositor_plan->window_policy;
  out->surface_policy = compositor_plan->surface_policy;
  out->compositor_policy = compositor_plan->compositor_policy;
}

int login_window_credential_screen_window_damage_plan_build(
    const struct login_window_credential_screen_window_compositor_plan
        *compositor_plan,
    struct login_window_credential_screen_window_damage_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_window_damage_plan_reset(
      out, compositor_plan ? 1 : 0);
  if (!compositor_plan) return 0;
  out->requested_action = compositor_plan->requested_action;
  out->window_surface_plan_available =
      compositor_plan->window_surface_plan_available ? 1 : 0;
  out->window_surface_plan_safe =
      compositor_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      compositor_plan->window_compositor_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_window_damage_plan_compositor_is_safe(
          compositor_plan)) {
    out->event_type = "credential-screen-window-damage-plan-unsafe";
    out->blocked_reason = "credential-window-damage-plan-unsafe";
    out->message =
        "Credential screen window damage plan unsafe; use text login.";
    return 0;
  }
  out->window_damage_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = compositor_plan->action_allowed ? 1 : 0;
  out->action_blocked = compositor_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = compositor_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_window_damage_plan_copy_safe_fields(
      compositor_plan, out);
  out->damage_allowed = 1;
  out->damage_ticket_selected = 1;
  out->damage_target_selected = 1;
  out->damage_error = 0;
  out->damage_policy = "full-window-damage-declarative";
  out->cache_policy = "window-damage-cache-disabled";
  out->event_type = "credential-screen-window-damage-plan-ready";
  out->state = "window-damage-ready";
  out->message =
      "Credential screen window damage ticket ready; no damage submitted.";
  out->blocked_reason = compositor_plan->blocked_reason;
  if (compositor_plan->submit_requested) {
    out->damage_ticket = "text-login-fallback-window-damage-ticket";
    out->compositor_target = "text-login-fallback-window-damage";
    out->damage_policy = "fallback-window-damage-declarative";
    out->damage_text_login = 1;
    out->damage_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->damage_cache_allowed = 0;
    out->damage_reuse_allowed = 0;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "window-damage-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (compositor_plan->compositor_credential_panel &&
      compositor_plan->compositor_credential_input &&
      compositor_plan->compositor_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->damage_ticket = "credential-screen-window-damage-ticket";
    out->compositor_target = "credential-screen-window-damage";
    out->damage_policy = "incremental-window-damage-declarative";
    out->cache_policy = "window-damage-cache-eligible";
    out->damage_incremental_allowed = 1;
    out->full_damage_required = 0;
    out->damage_cache_allowed = 1;
    out->damage_reuse_allowed = 1;
    out->damage_credential_panel = 1;
    out->damage_credential_input = 1;
    out->damage_credential_focus = 1;
    out->damage_text_login = 0;
    out->damage_text_login_fallback = 0;
    out->state = "window-damage-credential-ready";
    out->message = "Credential window damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (compositor_plan->compositor_text_recovery &&
      out->recovery_text_session_required) {
    out->damage_ticket = "text-recovery-window-damage-ticket";
    out->compositor_target = "text-recovery-window-damage";
    out->damage_text_recovery = 1;
    out->damage_text_login = 1;
    out->damage_text_login_fallback = 0;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->damage_cache_allowed = 0;
    out->damage_reuse_allowed = 0;
    out->input_focus_allowed = 0;
    out->state = "window-damage-text-recovery-ready";
    out->message = "Text recovery window damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (compositor_plan->compositor_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->damage_ticket = "text-login-resume-window-damage-ticket";
    out->compositor_target = "text-login-resume-window-damage";
    out->damage_policy = "full-window-damage-declarative";
    out->cache_policy = "window-damage-cache-bypassed-for-rerender";
    out->damage_text_login = 1;
    out->damage_text_login_resume = 1;
    out->damage_text_login_fallback = 0;
    out->full_damage_required = 1;
    out->damage_incremental_allowed = 0;
    out->damage_cache_allowed = 0;
    out->damage_reuse_allowed = 0;
    out->input_focus_allowed = 0;
    out->state = "window-damage-resume-ready";
    out->message = "Text login resume window damage ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->damage_ticket = "text-login-fallback-window-damage-ticket";
  out->compositor_target = "text-login-fallback-window-damage";
  out->damage_policy = "fallback-window-damage-declarative";
  out->damage_text_login = 1;
  out->damage_text_login_fallback = 1;
  out->full_damage_required = 1;
  out->damage_incremental_allowed = 0;
  out->damage_cache_allowed = 0;
  out->damage_reuse_allowed = 0;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "window-damage-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
