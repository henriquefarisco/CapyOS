/*
 * src/auth/login_runtime/reclaim_plan.c
 *
 * Credential-screen reclaim plan reset + safety predicate + copy
 * helper + builder — extracted byte-for-byte from
 * `src/auth/login_runtime.c` during PR C.45 of the Estagio C
 * dedicated plan.  Hosts every static helper that composes the
 * reclaim stage and the public builder that consumes them:
 *
 *   - login_window_credential_screen_reclaim_plan_reset (static)
 *   - login_window_credential_screen_reclaim_plan_compaction_is_safe (static)
 *   - login_window_credential_screen_reclaim_plan_copy_safe_fields (static)
 *   - login_window_credential_screen_reclaim_plan_build
 *
 * The reclaim-plan converts a fail-closed compaction-plan into a
 * reclaim contract for the downstream release stage.  The static
 * `_compaction_is_safe` predicate consolidates the upstream-safety
 * check and `_copy_safe_fields` performs the field-by-field
 * propagation used when the upstream contract is safe.  All helpers
 * are kept file-local so they cannot be reused outside this
 * translation unit; the public builder is the only entry point.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_reclaim_plan_reset(
    struct login_window_credential_screen_reclaim_plan *out,
    int compaction_plan_available) {
  *out = (struct login_window_credential_screen_reclaim_plan){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_RECLAIM_PLAN_VERSION;
  out->compaction_plan_available = compaction_plan_available ? 1 : 0;
  out->route_blocked = 1;
  out->action_blocked = 1;
  out->compaction_required = 1;
  out->reclaim_required = 1;
  out->reclaim_text_login = 1;
  out->reclaim_text_login_fallback = 1;
  out->reclaim_error = 1;
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
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->compaction_policy = "compaction-disabled";
  out->reclaim_policy = "reclaim-disabled";
  out->event_type = "credential-screen-reclaim-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "compaction-plan-unavailable";
}

static int login_window_credential_screen_reclaim_plan_compaction_is_safe(
    const struct login_window_credential_screen_compaction_plan *compaction_plan) {
  return compaction_plan->compaction_plan_safe &&
         compaction_plan->tombstone_required &&
         compaction_plan->tombstone_allowed &&
         !compaction_plan->tombstone_submitted &&
         compaction_plan->tombstone_ticket_selected &&
         compaction_plan->tombstone_target_selected &&
         !compaction_plan->tombstone_persist_allowed &&
         !compaction_plan->tombstone_persisted &&
         !compaction_plan->tombstone_cpu_gpu_sync_allowed &&
         !compaction_plan->tombstone_cpu_gpu_sync_submitted &&
         compaction_plan->compaction_required &&
         compaction_plan->compaction_allowed &&
         !compaction_plan->compaction_submitted &&
         compaction_plan->compaction_ticket_selected &&
         compaction_plan->compaction_target_selected &&
         !compaction_plan->compaction_storage_write_allowed &&
         !compaction_plan->compaction_storage_written &&
         !compaction_plan->compaction_resource_release_allowed &&
         !compaction_plan->compaction_resource_released &&
         !compaction_plan->compaction_cpu_gpu_sync_allowed &&
         !compaction_plan->compaction_cpu_gpu_sync_submitted &&
         !compaction_plan->compaction_error &&
         compaction_plan->route_selected &&
         !compaction_plan->route_blocked &&
         compaction_plan->credential_session_safe &&
         compaction_plan->credential_storage_wiped &&
         compaction_plan->credential_redacted &&
         compaction_plan->length_redacted &&
         !compaction_plan->raw_secret_exposed &&
         !compaction_plan->masked_text_exposed &&
         compaction_plan->submit_blocked &&
         !compaction_plan->submit_enabled &&
         !compaction_plan->auth_attempt_allowed &&
         !compaction_plan->submit_callback_bound &&
         !compaction_plan->auth_callback_bound &&
         compaction_plan->text_login_authoritative;
}

static void login_window_credential_screen_reclaim_plan_copy_safe_fields(
    const struct login_window_credential_screen_compaction_plan *compaction_plan,
    struct login_window_credential_screen_reclaim_plan *out) {
  out->compaction_required = compaction_plan->compaction_required ? 1 : 0;
  out->compaction_allowed = compaction_plan->compaction_allowed ? 1 : 0;
  out->compaction_ticket_selected =
      compaction_plan->compaction_ticket_selected ? 1 : 0;
  out->compaction_target_selected =
      compaction_plan->compaction_target_selected ? 1 : 0;
  out->recovery_text_session_required =
      compaction_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = compaction_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required =
      compaction_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = 1;
  out->credential_storage_wiped = 1;
  out->credential_redacted = 1;
  out->length_redacted = 1;
  out->submit_requested = compaction_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->text_login_authoritative = 1;
  out->view = compaction_plan->view;
  out->widget_tree = compaction_plan->widget_tree;
  out->compaction_ticket = compaction_plan->compaction_ticket;
  out->focus_target = compaction_plan->focus_target;
  out->primary_action = compaction_plan->primary_action;
  out->route = compaction_plan->route;
  out->compaction_policy = compaction_plan->compaction_policy;
}

int login_window_credential_screen_reclaim_plan_build(
    const struct login_window_credential_screen_compaction_plan *compaction_plan,
    struct login_window_credential_screen_reclaim_plan *out) {
  if (!out) return -1;
  login_window_credential_screen_reclaim_plan_reset(out,
                                                    compaction_plan ? 1 : 0);
  if (!compaction_plan) return 0;
  out->requested_action = compaction_plan->requested_action;
  out->compaction_plan_safe = compaction_plan->compaction_plan_safe ? 1 : 0;
  if (!login_window_credential_screen_reclaim_plan_compaction_is_safe(compaction_plan)) {
    out->event_type = "credential-screen-reclaim-plan-unsafe";
    out->blocked_reason = "credential-reclaim-plan-unsafe";
    out->message = "Credential screen reclaim plan unsafe; use text login.";
    return 0;
  }
  out->reclaim_plan_safe = 1;
  out->route_selected = 1;
  out->route_blocked = 0;
  out->action_allowed = compaction_plan->action_allowed ? 1 : 0;
  out->action_blocked = compaction_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = compaction_plan->input_focus_allowed ? 1 : 0;
  login_window_credential_screen_reclaim_plan_copy_safe_fields(compaction_plan,
                                                               out);
  out->reclaim_allowed = 1;
  out->reclaim_ticket_selected = 1;
  out->reclaim_target_selected = 1;
  out->reclaim_error = 0;
  out->reclaim_policy = "declarative-reclaim-no-release";
  out->event_type = "credential-screen-reclaim-plan-ready";
  out->state = "reclaim-ready";
  out->message = "Credential screen reclaim ticket ready; no resource reclaimed.";
  out->blocked_reason = compaction_plan->blocked_reason;
  if (compaction_plan->submit_requested) {
    out->reclaim_ticket = "text-login-fallback-reclaim-ticket";
    out->compositor_target = "text-login-fallback-reclaim";
    out->reclaim_policy = "fallback-reclaim-declarative";
    out->reclaim_text_login = 1;
    out->reclaim_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->state = "reclaim-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (compaction_plan->compaction_credential_panel &&
      compaction_plan->compaction_credential_input &&
      compaction_plan->compaction_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->reclaim_ticket = "credential-screen-reclaim-ticket";
    out->compositor_target = "credential-screen-reclaim";
    out->reclaim_credential_panel = 1;
    out->reclaim_credential_input = 1;
    out->reclaim_credential_focus = 1;
    out->reclaim_text_login = 0;
    out->reclaim_text_login_fallback = 0;
    out->state = "reclaim-credential-ready";
    out->message = "Credential reclaim ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (compaction_plan->compaction_text_recovery &&
      out->recovery_text_session_required) {
    out->reclaim_ticket = "text-recovery-reclaim-ticket";
    out->compositor_target = "text-recovery-reclaim";
    out->reclaim_text_recovery = 1;
    out->reclaim_text_login = 1;
    out->reclaim_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "reclaim-text-recovery-ready";
    out->message = "Text recovery reclaim ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (compaction_plan->compaction_text_login_resume &&
      out->session_reset_required && out->login_screen_rerender_required) {
    out->reclaim_ticket = "text-login-resume-reclaim-ticket";
    out->compositor_target = "text-login-resume-reclaim";
    out->reclaim_policy = "full-reclaim-declarative";
    out->reclaim_text_login = 1;
    out->reclaim_text_login_resume = 1;
    out->reclaim_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->state = "reclaim-resume-ready";
    out->message = "Text login resume reclaim ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  out->reclaim_ticket = "text-login-fallback-reclaim-ticket";
  out->compositor_target = "text-login-fallback-reclaim";
  out->reclaim_policy = "fallback-reclaim-declarative";
  out->reclaim_text_login = 1;
  out->reclaim_text_login_fallback = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->state = "reclaim-text-login-ready";
  out->message = "Use authoritative text login.";
  return 0;
}
