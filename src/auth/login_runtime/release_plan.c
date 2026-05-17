/*
 * src/auth/login_runtime/release_plan.c
 *
 * Credential-screen release plan reset + safety predicate + copy
 * helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.46 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * release stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_release_plan_reset (static)
 *   - login_window_credential_screen_release_plan_reclaim_is_safe (static)
 *   - login_window_credential_screen_release_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_release_plan_build
 *
 * The release-plan converts a fail-closed reclaim-plan into a
 * release contract for the downstream GUI stage.  The static
 * `_reclaim_is_safe` predicate consolidates the upstream-safety
 * check and `_copy_safe_fields` performs the field-by-field
 * propagation used when the upstream contract is safe.  All helpers
 * are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 * Closes Phase 5 of the Estagio C dedicated plan.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_release_plan_reset(
    struct login_window_credential_screen_release_plan *out,
    int reclaim_plan_available) {
  *out = (struct login_window_credential_screen_release_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RELEASE_PLAN_VERSION;
  out->reclaim_plan_available = reclaim_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->release_required = 1;
  out->release_text_login = 1;
  out->release_text_login_fallback = 1;
  out->release_error = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->compaction_policy = "compaction-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->release_policy = "release-disabled";
  out->event_type = "credential-screen-release-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "reclaim-plan-unavailable";
}

static int login_window_credential_screen_release_plan_reclaim_is_safe(
    const struct login_window_credential_screen_reclaim_plan *reclaim_plan) {
  return reclaim_plan->reclaim_plan_safe &&
         reclaim_plan->compaction_required &&
         reclaim_plan->compaction_allowed &&
         !reclaim_plan->compaction_submitted &&
         reclaim_plan->compaction_ticket_selected &&
         reclaim_plan->compaction_target_selected &&
         !reclaim_plan->compaction_storage_write_allowed &&
         !reclaim_plan->compaction_storage_written &&
         !reclaim_plan->compaction_resource_release_allowed &&
         !reclaim_plan->compaction_resource_released &&
         !reclaim_plan->compaction_cpu_gpu_sync_allowed &&
         !reclaim_plan->compaction_cpu_gpu_sync_submitted &&
         reclaim_plan->reclaim_required &&
         reclaim_plan->reclaim_allowed &&
         !reclaim_plan->reclaim_submitted &&
         reclaim_plan->reclaim_ticket_selected &&
         reclaim_plan->reclaim_target_selected &&
         !reclaim_plan->reclaim_storage_prune_allowed &&
         !reclaim_plan->reclaim_storage_pruned &&
         !reclaim_plan->reclaim_resource_release_allowed &&
         !reclaim_plan->reclaim_resource_released &&
         !reclaim_plan->reclaim_cpu_gpu_sync_allowed &&
         !reclaim_plan->reclaim_cpu_gpu_sync_submitted &&
         !reclaim_plan->reclaim_error &&
         reclaim_plan->route_selected &&
         !reclaim_plan->route_blocked &&
         reclaim_plan->credential_session_safe &&
         reclaim_plan->credential_storage_wiped &&
         reclaim_plan->credential_redacted &&
         reclaim_plan->length_redacted &&
         !reclaim_plan->raw_secret_exposed &&
         !reclaim_plan->masked_text_exposed &&
         reclaim_plan->submit_blocked &&
         !reclaim_plan->submit_enabled &&
         !reclaim_plan->auth_attempt_allowed &&
         !reclaim_plan->submit_callback_bound &&
         !reclaim_plan->auth_callback_bound &&
         reclaim_plan->text_login_authoritative;
}

static void login_window_credential_screen_release_plan_copy_safe_fields(
    const struct login_window_credential_screen_reclaim_plan *reclaim_plan,
    struct login_window_credential_screen_release_plan *out) {
  out->compaction_required = reclaim_plan->compaction_required ? 1 : 0;
  out->compaction_allowed = reclaim_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      reclaim_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      reclaim_plan->compaction_target_selected ? 1 : 0;
  out->reclaim_required = reclaim_plan->reclaim_required ? 1 : 0;
  out->reclaim_allowed = reclaim_plan->reclaim_allowed ? 1 : 0;
  out->reclaim_ticket_selected =
      reclaim_plan->reclaim_ticket_selected ? 1 : 0;
  out->reclaim_target_selected =
      reclaim_plan->reclaim_target_selected ? 1 : 0;
  out->recovery_text_session_required =
      reclaim_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = reclaim_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      reclaim_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = reclaim_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = reclaim_plan->view;
  out->widget_tree = reclaim_plan->widget_tree;
  out->compaction_ticket = reclaim_plan->compaction_ticket;
  out->reclaim_ticket = reclaim_plan->reclaim_ticket;
  out->focus_target = reclaim_plan->focus_target;
  out->primary_action = reclaim_plan->primary_action;
  out->route = reclaim_plan->route;
  out->compaction_policy = reclaim_plan->compaction_policy;
  out->reclaim_policy = reclaim_plan->reclaim_policy;
}

int login_window_credential_screen_release_plan_build(
    const struct login_window_credential_screen_reclaim_plan *reclaim_plan,
    struct login_window_credential_screen_release_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_release_plan_reset(out,
                                                   reclaim_plan ? 1 : 0);
  if (!reclaim_plan) return 0;
  out->requested_action = reclaim_plan->requested_action;
  out->reclaim_plan_safe = reclaim_plan->reclaim_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_release_plan_reclaim_is_safe(reclaim_plan)) {
    out->event_type = "credential-screen-release-plan-unsafe";
    out->blocked_reason = "credential-release-plan-unsafe";
    out->message = "Credential screen release plan unsafe; use text login.";
    return 0;
  }
  out->release_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = reclaim_plan->action_allowed ? 1 : 0;
  out->action_blocked = reclaim_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = reclaim_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_release_plan_copy_safe_fields(reclaim_plan,
                                                               out);
  out->release_allowed = 1;
  out->release_ticket_selected = 1;
  out->release_target_selected = 1;
  out->release_error = 0;
  out->release_policy = "declarative-release-no-resource";
  out->event_type = "credential-screen-release-plan-ready";
  out->state = "release-ready";
  out->message = "Credential screen release ticket ready; no resource released.";
  out->blocked_reason = reclaim_plan->blocked_reason;
  if (reclaim_plan->submit_requested) {
    out->release_ticket = "text-login-fallback-release-ticket";
    out->compositor_target = "text-login-fallback-release";
    out->release_policy = "fallback-release-declarative";
    out->release_text_login = 1;
    out->release_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "release-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (reclaim_plan->reclaim_credential_panel &&
      reclaim_plan->reclaim_credential_input &&
      reclaim_plan->reclaim_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->release_ticket = "credential-screen-release-ticket";
    out->compositor_target = "credential-screen-release";
    out->release_credential_panel = 1;
    out->release_credential_input = 1;
    out->release_credential_focus = 1;
    out->release_text_login = 0;
    out->release_text_login_fallback = 0;
    out->state = "release-credential-ready";
    out->message = "Credential release ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (reclaim_plan->reclaim_text_recovery &&
      out->recovery_text_session_required) {
    out->release_ticket = "text-recovery-release-ticket";
    out->compositor_target = "text-recovery-release";
    out->release_text_recovery = 1;
    out->release_text_login = 1;
    out->release_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "release-text-recovery-ready";
    out->message = "Text recovery release ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (reclaim_plan->reclaim_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->release_ticket = "text-login-resume-release-ticket";
    out->compositor_target = "text-login-resume-release";
    out->release_policy = "full-release-declarative";
    out->release_text_login = 1;
    out->release_text_login_resume = 1;
    out->release_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "release-resume-ready";
    out->message = "Text login resume release ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->release_ticket = "text-login-fallback-release-ticket";
  out->compositor_target = "text-login-fallback-release";
  out->release_policy = "fallback-release-declarative";
  out->release_text_login = 1;
  out->release_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "release-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
