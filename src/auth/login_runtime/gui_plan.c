/*
 * src/auth/login_runtime/gui_plan.c
 *
 * Credential-screen GUI plan reset + safety predicate + copy helper
 * + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.47 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the GUI
 * stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_gui_plan_reset (static)
 *   - login_window_credential_screen_gui_plan_release_is_safe (static)
 *   - login_window_credential_screen_gui_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_gui_plan_build
 *
 * The GUI-plan converts a fail-closed release-plan into a GUI
 * contract for the downstream window stage.  The static
 * `_release_is_safe` predicate consolidates the upstream-safety
 * check and `_copy_safe_fields` performs the field-by-field
 * propagation used when the upstream contract is safe.  All helpers
 * are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 * Opens Phase 6 of the Estagio C dedicated plan (GUI/Window).
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_gui_plan_reset(
    struct login_window_credential_screen_gui_plan *out,
    int release_plan_available) {
  *out = (struct login_window_credential_screen_gui_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_GUI_PLAN_VERSION;
  out->release_plan_available = release_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->release_required = 1;
  out->gui_required = 1;
  out->gui_text_login = 1;
  out->gui_text_login_fallback = 1;
  out->gui_error = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->compaction_policy = "compaction-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->release_policy = "release-disabled";
  out->gui_policy = "gui-disabled";
  out->event_type = "credential-screen-gui-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "release-plan-unavailable";
}

static int login_window_credential_screen_gui_plan_release_is_safe(
    const struct login_window_credential_screen_release_plan *release_plan) {
  return release_plan->release_plan_safe &&
         release_plan->compaction_required &&
         release_plan->compaction_allowed &&
         !release_plan->compaction_submitted &&
         release_plan->compaction_ticket_selected &&
         release_plan->compaction_target_selected &&
         !release_plan->compaction_storage_write_allowed &&
         !release_plan->compaction_storage_written &&
         !release_plan->compaction_resource_release_allowed &&
         !release_plan->compaction_resource_released &&
         !release_plan->compaction_cpu_gpu_sync_allowed &&
         !release_plan->compaction_cpu_gpu_sync_submitted &&
         release_plan->reclaim_required &&
         release_plan->reclaim_allowed &&
         !release_plan->reclaim_submitted &&
         release_plan->reclaim_ticket_selected &&
         release_plan->reclaim_target_selected &&
         !release_plan->reclaim_storage_prune_allowed &&
         !release_plan->reclaim_storage_pruned &&
         !release_plan->reclaim_resource_release_allowed &&
         !release_plan->reclaim_resource_released &&
         !release_plan->reclaim_cpu_gpu_sync_allowed &&
         !release_plan->reclaim_cpu_gpu_sync_submitted &&
         release_plan->release_required &&
         release_plan->release_allowed &&
         !release_plan->release_submitted &&
         release_plan->release_ticket_selected &&
         release_plan->release_target_selected &&
         !release_plan->release_storage_prune_allowed &&
         !release_plan->release_storage_pruned &&
         !release_plan->release_resource_release_allowed &&
         !release_plan->release_resource_released &&
         !release_plan->release_cpu_gpu_sync_allowed &&
         !release_plan->release_cpu_gpu_sync_submitted &&
         !release_plan->release_error &&
         release_plan->route_selected &&
         !release_plan->route_blocked &&
         release_plan->credential_session_safe &&
         release_plan->credential_storage_wiped &&
         release_plan->credential_redacted &&
         release_plan->length_redacted &&
         !release_plan->raw_secret_exposed &&
         !release_plan->masked_text_exposed &&
         release_plan->submit_blocked &&
         !release_plan->submit_enabled &&
         !release_plan->auth_attempt_allowed &&
         !release_plan->submit_callback_bound &&
         !release_plan->auth_callback_bound &&
         release_plan->text_login_authoritative;
}

static void login_window_credential_screen_gui_plan_copy_safe_fields(
    const struct login_window_credential_screen_release_plan *release_plan,
    struct login_window_credential_screen_gui_plan *out) {
  out->compaction_required = release_plan->compaction_required ? 1 : 0;
  out->compaction_allowed = release_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      release_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      release_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_required = release_plan->reclaim_required ? 1 : 0;
  out->reclaim_allowed = release_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected =
      release_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected =
      release_plan->reclaim_target_selected ? 1 : 0;
  out->release_required = release_plan->release_required ? 1 : 0;
  out->release_allowed = release_plan->release_allowed ? 1 : 0;
  out->release_ticket_selected =
      release_plan->release_ticket_selected ? 1 : 0;
  out->release_target_selected =
      release_plan->release_target_selected ? 1 : 0;
  out->recovery_text_session_required =
      release_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = release_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      release_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = release_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = release_plan->view;
  out->widget_tree = release_plan->widget_tree;
  out->compaction_ticket = release_plan->compaction_ticket;
  out->reclaim_ticket = release_plan->reclaim_ticket;
  out->release_ticket = release_plan->release_ticket;
  out->focus_target = release_plan->focus_target;
  out->primary_action = release_plan->primary_action;
  out->route = release_plan->route;
  out->compaction_policy = release_plan->compaction_policy;
  out->reclaim_policy = release_plan->reclaim_policy;
  out->release_policy = release_plan->release_policy;
}

int login_window_credential_screen_gui_plan_build(
    const struct login_window_credential_screen_release_plan *release_plan,
    struct login_window_credential_screen_gui_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_gui_plan_reset(out, release_plan ? 1 : 0);
  if (!release_plan) return 0;
  out->requested_action = release_plan->requested_action;
  out->release_plan_safe = release_plan->release_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_gui_plan_release_is_safe(release_plan)) {
    out->event_type = "credential-screen-gui-plan-unsafe";
    out->blocked_reason = "credential-gui-plan-unsafe";
    out->message = "Credential screen GUI plan unsafe; use text login.";
    return 0;
  }
  out->gui_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = release_plan->action_allowed ? 1 : 0;
  out->action_blocked = release_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = release_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_gui_plan_copy_safe_fields(release_plan, out);
  out->gui_allowed = 1;
  out->gui_ticket_selected = 1;
  out->gui_target_selected = 1;
  out->gui_error = 0;
  out->gui_policy = "declarative-gui-no-pixels";
  out->event_type = "credential-screen-gui-plan-ready";
  out->state = "gui-ready";
  out->message = "Credential screen GUI ticket ready; no pixels written.";
  out->blocked_reason = release_plan->blocked_reason;
  if (release_plan->submit_requested) {
    out->gui_ticket = "text-login-fallback-gui-ticket";
    out->compositor_target = "text-login-fallback-gui";
    out->gui_policy = "fallback-gui-declarative";
    out->gui_text_login = 1;
    out->gui_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "gui-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (release_plan->release_credential_panel &&
      release_plan->release_credential_input &&
      release_plan->release_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->gui_ticket = "credential-screen-gui-ticket";
    out->compositor_target = "credential-screen-gui";
    out->gui_credential_panel = 1;
    out->gui_credential_input = 1;
    out->gui_credential_focus = 1;
    out->gui_text_login = 0;
    out->gui_text_login_fallback = 0;
    out->state = "gui-credential-ready";
    out->message = "Credential GUI ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (release_plan->release_text_recovery &&
      out->recovery_text_session_required) {
    out->gui_ticket = "text-recovery-gui-ticket";
    out->compositor_target = "text-recovery-gui";
    out->gui_text_recovery = 1;
    out->gui_text_login = 1;
    out->gui_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "gui-text-recovery-ready";
    out->message = "Text recovery GUI ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (release_plan->release_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->gui_ticket = "text-login-resume-gui-ticket";
    out->compositor_target = "text-login-resume-gui";
    out->gui_policy = "full-gui-declarative";
    out->gui_text_login = 1;
    out->gui_text_login_resume = 1;
    out->gui_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "gui-resume-ready";
    out->message = "Text login resume GUI ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->gui_ticket = "text-login-fallback-gui-ticket";
  out->compositor_target = "text-login-fallback-gui";
  out->gui_policy = "fallback-gui-declarative";
  out->gui_text_login = 1;
  out->gui_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "gui-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
