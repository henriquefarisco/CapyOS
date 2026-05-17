/*
 * src/auth/login_runtime/handoff_dispatch.c
 *
 * Credential-screen handoff + dispatch plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.11 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that move a committed credential screen into the
 * cross-actor handoff and dispatch stages:
 *
 *   - login_window_credential_screen_handoff_plan_build
 *   - login_window_credential_screen_dispatch_plan_build
 *
 * The handoff-plan owns the transfer of authority from the screen's
 * mounted commit-plan to the next actor in the pipeline; the
 * dispatch-plan owns the queueing of the resulting message back to
 * the credential service.  Both are fail-closed.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_handoff_plan_build(
    const struct login_window_credential_screen_commit_plan *commit_plan,
    struct login_window_credential_screen_handoff_plan *out) {
  int commit_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_HANDOFF_PLAN_VERSION;
  out->commit_plan_available = commit_plan ? 1 : 0;
  out->commit_plan_safe = 0;
  out->handoff_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_handoff_required = 1;
  out->window_handoff_allowed = 0;
  out->window_handoff_delivered = 0;
  out->envelope_selected = 0;
  out->handoff_credential_panel = 0;
  out->handoff_credential_input = 0;
  out->handoff_credential_focus = 0;
  out->handoff_text_recovery = 0;
  out->handoff_text_login = 1;
  out->handoff_text_login_resume = 0;
  out->handoff_text_login_fallback = 1;
  out->handoff_fallback_notice = 1;
  out->handoff_text_login_notice = 1;
  out->handoff_status = 1;
  out->handoff_error = 1;
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
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-handoff-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "commit-plan-unavailable";
  if (!commit_plan) return 0;
  out->requested_action = commit_plan->requested_action;
  out->commit_plan_safe = commit_plan->commit_plan_safe ? 1 : 0;
  out->route_selected = commit_plan->route_selected ? 1 : 0;
  out->route_blocked = commit_plan->route_blocked ? 1 : 0;
  out->action_allowed = commit_plan->action_allowed ? 1 : 0;
  out->action_blocked = commit_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = commit_plan->input_focus_allowed ? 1 : 0;
  out->envelope_selected = commit_plan->widget_tree_selected ? 1 : 0;
  out->recovery_text_session_required = commit_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = commit_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = commit_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = commit_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = commit_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = commit_plan->credential_redacted ? 1 : 0;
  out->length_redacted = commit_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = commit_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = commit_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = commit_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = commit_plan->text_login_authoritative ? 1 : 0;
  out->view = commit_plan->view ? commit_plan->view : "text-login-fallback";
  out->widget_tree = commit_plan->widget_tree ? commit_plan->widget_tree
                                              : "text-login-fallback-bindings";
  out->mount_transaction = commit_plan->mount_transaction ? commit_plan->mount_transaction
                                                          : "text-login-fallback-mount-plan";
  out->commit_transaction = commit_plan->commit_transaction ? commit_plan->commit_transaction
                                                            : "text-login-fallback-commit-plan";
  out->focus_target = commit_plan->focus_target ? commit_plan->focus_target : "none";
  out->primary_action = commit_plan->primary_action ? commit_plan->primary_action
                                                    : "use-text-login";
  out->route = commit_plan->route ? commit_plan->route : "force-text-login";
  out->event_type = commit_plan->event_type ? commit_plan->event_type
                                            : "credential-screen-commit-plan-blocked";
  out->state = commit_plan->state ? commit_plan->state : "blocked";
  out->message = commit_plan->message ? commit_plan->message
                                      : "Text login remains authoritative.";
  out->blocked_reason = commit_plan->blocked_reason ? commit_plan->blocked_reason
                                                    : "blocked";
  commit_safe = out->commit_plan_safe && commit_plan->window_commit_allowed &&
                !commit_plan->window_commit_executed && out->envelope_selected &&
                out->route_selected && !out->route_blocked &&
                out->credential_session_safe && out->credential_storage_wiped &&
                out->credential_redacted && out->length_redacted &&
                !out->raw_secret_exposed && !out->masked_text_exposed &&
                commit_plan->submit_blocked && !commit_plan->submit_enabled &&
                !commit_plan->auth_attempt_allowed &&
                !commit_plan->submit_callback_bound && !commit_plan->auth_callback_bound &&
                out->text_login_authoritative;
  if (!commit_safe) {
    out->event_type = "credential-screen-handoff-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen handoff plan unsafe; use text login.";
    out->blocked_reason = "credential-handoff-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_handoff_allowed = 0;
    out->window_handoff_delivered = 0;
    out->envelope_selected = 0;
    out->handoff_credential_panel = 0;
    out->handoff_credential_input = 0;
    out->handoff_credential_focus = 0;
    out->handoff_text_recovery = 0;
    out->handoff_text_login = 1;
    out->handoff_text_login_resume = 0;
    out->handoff_text_login_fallback = 1;
    out->handoff_fallback_notice = 1;
    out->handoff_text_login_notice = 1;
    out->handoff_status = 1;
    out->handoff_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->handoff_plan_safe = 1;
  out->window_handoff_allowed = 1;
  out->window_handoff_delivered = 0;
  out->handoff_error = 0;
  if (commit_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->handoff_text_login = 1;
    out->handoff_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->handoff_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "handoff-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (commit_plan->commit_credential_panel && commit_plan->commit_credential_input &&
      commit_plan->commit_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->handoff_envelope = "credential-screen-handoff-envelope";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->handoff_credential_panel = 1;
    out->handoff_credential_input = 1;
    out->handoff_credential_focus = 1;
    out->handoff_text_login = 0;
    out->handoff_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "handoff-credential-ready";
    out->message = "Credential handoff envelope ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (commit_plan->commit_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->handoff_envelope = "text-recovery-handoff-envelope";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->handoff_text_recovery = 1;
    out->handoff_text_login = 1;
    out->handoff_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->handoff_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "handoff-text-recovery-ready";
    out->message = "Text recovery handoff envelope ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (commit_plan->commit_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->handoff_envelope = "text-login-resume-handoff-envelope";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->handoff_text_login = 1;
    out->handoff_text_login_resume = 1;
    out->handoff_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->handoff_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "handoff-resume-ready";
    out->message = "Text login resume handoff envelope ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (commit_plan->commit_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->handoff_text_login = 1;
    out->handoff_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->handoff_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "handoff-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-handoff-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen handoff plan blocked; use text login.";
  out->blocked_reason = "credential-handoff-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->handoff_credential_focus = 0;
  out->handoff_text_login = 1;
  out->handoff_text_login_fallback = 1;
  out->handoff_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}


int login_window_credential_screen_dispatch_plan_build(
    const struct login_window_credential_screen_handoff_plan *handoff_plan,
    struct login_window_credential_screen_dispatch_plan *out) {
  int handoff_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPATCH_PLAN_VERSION;
  out->handoff_plan_available = handoff_plan ? 1 : 0;
  out->handoff_plan_safe = 0;
  out->dispatch_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_dispatch_required = 1;
  out->window_dispatch_allowed = 0;
  out->window_dispatch_delivered = 0;
  out->dispatch_ticket_selected = 0;
  out->dispatch_credential_panel = 0;
  out->dispatch_credential_input = 0;
  out->dispatch_credential_focus = 0;
  out->dispatch_text_recovery = 0;
  out->dispatch_text_login = 1;
  out->dispatch_text_login_resume = 0;
  out->dispatch_text_login_fallback = 1;
  out->dispatch_fallback_notice = 1;
  out->dispatch_text_login_notice = 1;
  out->dispatch_status = 1;
  out->dispatch_error = 1;
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
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-dispatch-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "handoff-plan-unavailable";
  if (!handoff_plan) return 0;
  out->requested_action = handoff_plan->requested_action;
  out->handoff_plan_safe = handoff_plan->handoff_plan_safe ? 1 : 0;
  out->route_selected = handoff_plan->route_selected ? 1 : 0;
  out->route_blocked = handoff_plan->route_blocked ? 1 : 0;
  out->action_allowed = handoff_plan->action_allowed ? 1 : 0;
  out->action_blocked = handoff_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = handoff_plan->input_focus_allowed ? 1 : 0;
  out->dispatch_ticket_selected = handoff_plan->envelope_selected ? 1 : 0;
  out->recovery_text_session_required = handoff_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = handoff_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = handoff_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = handoff_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = handoff_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = handoff_plan->credential_redacted ? 1 : 0;
  out->length_redacted = handoff_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = handoff_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = handoff_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = handoff_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = handoff_plan->text_login_authoritative ? 1 : 0;
  out->view = handoff_plan->view ? handoff_plan->view : "text-login-fallback";
  out->widget_tree = handoff_plan->widget_tree ? handoff_plan->widget_tree
                                               : "text-login-fallback-bindings";
  out->mount_transaction = handoff_plan->mount_transaction ? handoff_plan->mount_transaction
                                                           : "text-login-fallback-mount-plan";
  out->commit_transaction = handoff_plan->commit_transaction ? handoff_plan->commit_transaction
                                                             : "text-login-fallback-commit-plan";
  out->handoff_envelope = handoff_plan->handoff_envelope ? handoff_plan->handoff_envelope
                                                         : "text-login-fallback-handoff-envelope";
  out->focus_target = handoff_plan->focus_target ? handoff_plan->focus_target : "none";
  out->primary_action = handoff_plan->primary_action ? handoff_plan->primary_action
                                                     : "use-text-login";
  out->route = handoff_plan->route ? handoff_plan->route : "force-text-login";
  out->event_type = handoff_plan->event_type ? handoff_plan->event_type
                                             : "credential-screen-handoff-plan-blocked";
  out->state = handoff_plan->state ? handoff_plan->state : "blocked";
  out->message = handoff_plan->message ? handoff_plan->message
                                       : "Text login remains authoritative.";
  out->blocked_reason = handoff_plan->blocked_reason ? handoff_plan->blocked_reason
                                                     : "blocked";
  handoff_safe = out->handoff_plan_safe && handoff_plan->window_handoff_allowed &&
                 !handoff_plan->window_handoff_delivered &&
                 out->dispatch_ticket_selected && out->route_selected &&
                 !out->route_blocked && out->credential_session_safe &&
                 out->credential_storage_wiped && out->credential_redacted &&
                 out->length_redacted && !out->raw_secret_exposed &&
                 !out->masked_text_exposed && handoff_plan->submit_blocked &&
                 !handoff_plan->submit_enabled && !handoff_plan->auth_attempt_allowed &&
                 !handoff_plan->submit_callback_bound && !handoff_plan->auth_callback_bound &&
                 out->text_login_authoritative;
  if (!handoff_safe) {
    out->event_type = "credential-screen-dispatch-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen dispatch plan unsafe; use text login.";
    out->blocked_reason = "credential-dispatch-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_dispatch_allowed = 0;
    out->window_dispatch_delivered = 0;
    out->dispatch_ticket_selected = 0;
    out->dispatch_credential_panel = 0;
    out->dispatch_credential_input = 0;
    out->dispatch_credential_focus = 0;
    out->dispatch_text_recovery = 0;
    out->dispatch_text_login = 1;
    out->dispatch_text_login_resume = 0;
    out->dispatch_text_login_fallback = 1;
    out->dispatch_fallback_notice = 1;
    out->dispatch_text_login_notice = 1;
    out->dispatch_status = 1;
    out->dispatch_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->dispatch_plan_safe = 1;
  out->window_dispatch_allowed = 1;
  out->window_dispatch_delivered = 0;
  out->dispatch_error = 0;
  if (handoff_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->dispatch_text_login = 1;
    out->dispatch_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->dispatch_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "dispatch-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (handoff_plan->handoff_credential_panel && handoff_plan->handoff_credential_input &&
      handoff_plan->handoff_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->handoff_envelope = "credential-screen-handoff-envelope";
    out->dispatch_ticket = "credential-screen-dispatch-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->dispatch_credential_panel = 1;
    out->dispatch_credential_input = 1;
    out->dispatch_credential_focus = 1;
    out->dispatch_text_login = 0;
    out->dispatch_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "dispatch-credential-ready";
    out->message = "Credential dispatch ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (handoff_plan->handoff_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->handoff_envelope = "text-recovery-handoff-envelope";
    out->dispatch_ticket = "text-recovery-dispatch-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->dispatch_text_recovery = 1;
    out->dispatch_text_login = 1;
    out->dispatch_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->dispatch_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "dispatch-text-recovery-ready";
    out->message = "Text recovery dispatch ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (handoff_plan->handoff_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->handoff_envelope = "text-login-resume-handoff-envelope";
    out->dispatch_ticket = "text-login-resume-dispatch-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->dispatch_text_login = 1;
    out->dispatch_text_login_resume = 1;
    out->dispatch_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->dispatch_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "dispatch-resume-ready";
    out->message = "Text login resume dispatch ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (handoff_plan->handoff_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->dispatch_text_login = 1;
    out->dispatch_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->dispatch_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "dispatch-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-dispatch-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen dispatch plan blocked; use text login.";
  out->blocked_reason = "credential-dispatch-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->dispatch_credential_focus = 0;
  out->dispatch_text_login = 1;
  out->dispatch_text_login_fallback = 1;
  out->dispatch_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}
