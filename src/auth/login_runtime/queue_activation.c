/*
 * src/auth/login_runtime/queue_activation.c
 *
 * Credential-screen queue + activation plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.12 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that move a dispatched credential message into the
 * service queue and prepare its activation:
 *
 *   - login_window_credential_screen_queue_plan_build
 *   - login_window_credential_screen_activation_plan_build
 *
 * The queue-plan owns the inbound queueing semantics of the
 * credential dispatch envelope; the activation-plan owns the
 * decision to bring the queued message to life as a runtime actor.
 * Both are fail-closed.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_queue_plan_build(
    const struct login_window_credential_screen_dispatch_plan *dispatch_plan,
    struct login_window_credential_screen_queue_plan *out) {
  int dispatch_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_QUEUE_PLAN_VERSION;
  out->dispatch_plan_available = dispatch_plan ? 1 : 0;
  out->dispatch_plan_safe = 0;
  out->queue_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_queue_required = 1;
  out->window_queue_allowed = 0;
  out->window_queue_enqueued = 0;
  out->queue_ticket_selected = 0;
  out->queue_credential_panel = 0;
  out->queue_credential_input = 0;
  out->queue_credential_focus = 0;
  out->queue_text_recovery = 0;
  out->queue_text_login = 1;
  out->queue_text_login_resume = 0;
  out->queue_text_login_fallback = 1;
  out->queue_fallback_notice = 1;
  out->queue_text_login_notice = 1;
  out->queue_status = 1;
  out->queue_error = 1;
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
  out->queue_ticket = "text-login-fallback-queue-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-queue-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "dispatch-plan-unavailable";
  if (!dispatch_plan) return 0;
  out->requested_action = dispatch_plan->requested_action;
  out->dispatch_plan_safe = dispatch_plan->dispatch_plan_safe ? 1 : 0;
  out->route_selected = dispatch_plan->route_selected ? 1 : 0;
  out->route_blocked = dispatch_plan->route_blocked ? 1 : 0;
  out->action_allowed = dispatch_plan->action_allowed ? 1 : 0;
  out->action_blocked = dispatch_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = dispatch_plan->input_focus_allowed ? 1 : 0;
  out->queue_ticket_selected = dispatch_plan->dispatch_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = dispatch_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = dispatch_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = dispatch_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = dispatch_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = dispatch_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = dispatch_plan->credential_redacted ? 1 : 0;
  out->length_redacted = dispatch_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = dispatch_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = dispatch_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = dispatch_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = dispatch_plan->text_login_authoritative ? 1 : 0;
  out->view = dispatch_plan->view ? dispatch_plan->view : "text-login-fallback";
  out->widget_tree = dispatch_plan->widget_tree ? dispatch_plan->widget_tree
                                                : "text-login-fallback-bindings";
  out->mount_transaction = dispatch_plan->mount_transaction ? dispatch_plan->mount_transaction
                                                            : "text-login-fallback-mount-plan";
  out->commit_transaction = dispatch_plan->commit_transaction ? dispatch_plan->commit_transaction
                                                              : "text-login-fallback-commit-plan";
  out->handoff_envelope = dispatch_plan->handoff_envelope ? dispatch_plan->handoff_envelope
                                                          : "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = dispatch_plan->dispatch_ticket ? dispatch_plan->dispatch_ticket
                                                        : "text-login-fallback-dispatch-ticket";
  out->focus_target = dispatch_plan->focus_target ? dispatch_plan->focus_target : "none";
  out->primary_action = dispatch_plan->primary_action ? dispatch_plan->primary_action
                                                      : "use-text-login";
  out->route = dispatch_plan->route ? dispatch_plan->route : "force-text-login";
  out->event_type = dispatch_plan->event_type ? dispatch_plan->event_type
                                              : "credential-screen-dispatch-plan-blocked";
  out->state = dispatch_plan->state ? dispatch_plan->state : "blocked";
  out->message = dispatch_plan->message ? dispatch_plan->message
                                        : "Text login remains authoritative.";
  out->blocked_reason = dispatch_plan->blocked_reason ? dispatch_plan->blocked_reason
                                                      : "blocked";
  dispatch_safe = out->dispatch_plan_safe && dispatch_plan->window_dispatch_allowed &&
                  !dispatch_plan->window_dispatch_delivered &&
                  out->queue_ticket_selected && out->route_selected &&
                  !out->route_blocked && out->credential_session_safe &&
                  out->credential_storage_wiped && out->credential_redacted &&
                  out->length_redacted && !out->raw_secret_exposed &&
                  !out->masked_text_exposed && dispatch_plan->submit_blocked &&
                  !dispatch_plan->submit_enabled && !dispatch_plan->auth_attempt_allowed &&
                  !dispatch_plan->submit_callback_bound && !dispatch_plan->auth_callback_bound &&
                  out->text_login_authoritative;
  if (!dispatch_safe) {
    out->event_type = "credential-screen-queue-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen queue plan unsafe; use text login.";
    out->blocked_reason = "credential-queue-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_queue_allowed = 0;
    out->window_queue_enqueued = 0;
    out->queue_ticket_selected = 0;
    out->queue_credential_panel = 0;
    out->queue_credential_input = 0;
    out->queue_credential_focus = 0;
    out->queue_text_recovery = 0;
    out->queue_text_login = 1;
    out->queue_text_login_resume = 0;
    out->queue_text_login_fallback = 1;
    out->queue_fallback_notice = 1;
    out->queue_text_login_notice = 1;
    out->queue_status = 1;
    out->queue_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->queue_plan_safe = 1;
  out->window_queue_allowed = 1;
  out->window_queue_enqueued = 0;
  out->queue_error = 0;
  if (dispatch_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->queue_text_login = 1;
    out->queue_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->queue_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "queue-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (dispatch_plan->dispatch_credential_panel && dispatch_plan->dispatch_credential_input &&
      dispatch_plan->dispatch_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->handoff_envelope = "credential-screen-handoff-envelope";
    out->dispatch_ticket = "credential-screen-dispatch-ticket";
    out->queue_ticket = "credential-screen-queue-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->queue_credential_panel = 1;
    out->queue_credential_input = 1;
    out->queue_credential_focus = 1;
    out->queue_text_login = 0;
    out->queue_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "queue-credential-ready";
    out->message = "Credential queue ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (dispatch_plan->dispatch_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->handoff_envelope = "text-recovery-handoff-envelope";
    out->dispatch_ticket = "text-recovery-dispatch-ticket";
    out->queue_ticket = "text-recovery-queue-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->queue_text_recovery = 1;
    out->queue_text_login = 1;
    out->queue_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->queue_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "queue-text-recovery-ready";
    out->message = "Text recovery queue ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (dispatch_plan->dispatch_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->handoff_envelope = "text-login-resume-handoff-envelope";
    out->dispatch_ticket = "text-login-resume-dispatch-ticket";
    out->queue_ticket = "text-login-resume-queue-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->queue_text_login = 1;
    out->queue_text_login_resume = 1;
    out->queue_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->queue_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "queue-resume-ready";
    out->message = "Text login resume queue ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (dispatch_plan->dispatch_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->queue_text_login = 1;
    out->queue_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->queue_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "queue-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-queue-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen queue plan blocked; use text login.";
  out->blocked_reason = "credential-queue-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
  out->queue_ticket = "text-login-fallback-queue-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->queue_credential_focus = 0;
  out->queue_text_login = 1;
  out->queue_text_login_fallback = 1;
  out->queue_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}


int login_window_credential_screen_activation_plan_build(
    const struct login_window_credential_screen_queue_plan *queue_plan,
    struct login_window_credential_screen_activation_plan *out) {
  int queue_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTIVATION_PLAN_VERSION;
  out->queue_plan_available = queue_plan ? 1 : 0;
  out->queue_plan_safe = 0;
  out->activation_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_activation_required = 1;
  out->window_activation_allowed = 0;
  out->window_activation_applied = 0;
  out->activation_ticket_selected = 0;
  out->activate_credential_panel = 0;
  out->activate_credential_input = 0;
  out->activate_credential_focus = 0;
  out->activate_text_recovery = 0;
  out->activate_text_login = 1;
  out->activate_text_login_resume = 0;
  out->activate_text_login_fallback = 1;
  out->activate_fallback_notice = 1;
  out->activate_text_login_notice = 1;
  out->activation_status = 1;
  out->activation_error = 1;
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
  out->queue_ticket = "text-login-fallback-queue-ticket";
  out->activation_ticket = "text-login-fallback-activation-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-activation-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "queue-plan-unavailable";
  if (!queue_plan) return 0;
  out->requested_action = queue_plan->requested_action;
  out->queue_plan_safe = queue_plan->queue_plan_safe ? 1 : 0;
  out->route_selected = queue_plan->route_selected ? 1 : 0;
  out->route_blocked = queue_plan->route_blocked ? 1 : 0;
  out->action_allowed = queue_plan->action_allowed ? 1 : 0;
  out->action_blocked = queue_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = queue_plan->input_focus_allowed ? 1 : 0;
  out->activation_ticket_selected = queue_plan->queue_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = queue_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = queue_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = queue_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = queue_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = queue_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = queue_plan->credential_redacted ? 1 : 0;
  out->length_redacted = queue_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = queue_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = queue_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = queue_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = queue_plan->text_login_authoritative ? 1 : 0;
  out->view = queue_plan->view ? queue_plan->view : "text-login-fallback";
  out->widget_tree = queue_plan->widget_tree ? queue_plan->widget_tree
                                             : "text-login-fallback-bindings";
  out->mount_transaction = queue_plan->mount_transaction ? queue_plan->mount_transaction
                                                         : "text-login-fallback-mount-plan";
  out->commit_transaction = queue_plan->commit_transaction ? queue_plan->commit_transaction
                                                           : "text-login-fallback-commit-plan";
  out->handoff_envelope = queue_plan->handoff_envelope ? queue_plan->handoff_envelope
                                                       : "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = queue_plan->dispatch_ticket ? queue_plan->dispatch_ticket
                                                     : "text-login-fallback-dispatch-ticket";
  out->queue_ticket = queue_plan->queue_ticket ? queue_plan->queue_ticket
                                               : "text-login-fallback-queue-ticket";
  out->focus_target = queue_plan->focus_target ? queue_plan->focus_target : "none";
  out->primary_action = queue_plan->primary_action ? queue_plan->primary_action
                                                   : "use-text-login";
  out->route = queue_plan->route ? queue_plan->route : "force-text-login";
  out->event_type = queue_plan->event_type ? queue_plan->event_type
                                           : "credential-screen-queue-plan-blocked";
  out->state = queue_plan->state ? queue_plan->state : "blocked";
  out->message = queue_plan->message ? queue_plan->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = queue_plan->blocked_reason ? queue_plan->blocked_reason
                                                   : "blocked";
  queue_safe = out->queue_plan_safe && queue_plan->window_queue_allowed &&
               !queue_plan->window_queue_enqueued && out->activation_ticket_selected &&
               out->route_selected && !out->route_blocked &&
               out->credential_session_safe && out->credential_storage_wiped &&
               out->credential_redacted && out->length_redacted &&
               !out->raw_secret_exposed && !out->masked_text_exposed &&
               queue_plan->submit_blocked && !queue_plan->submit_enabled &&
               !queue_plan->auth_attempt_allowed && !queue_plan->submit_callback_bound &&
               !queue_plan->auth_callback_bound && out->text_login_authoritative;
  if (!queue_safe) {
    out->event_type = "credential-screen-activation-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen activation plan unsafe; use text login.";
    out->blocked_reason = "credential-activation-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_activation_allowed = 0;
    out->window_activation_applied = 0;
    out->activation_ticket_selected = 0;
    out->activate_credential_panel = 0;
    out->activate_credential_input = 0;
    out->activate_credential_focus = 0;
    out->activate_text_recovery = 0;
    out->activate_text_login = 1;
    out->activate_text_login_resume = 0;
    out->activate_text_login_fallback = 1;
    out->activate_fallback_notice = 1;
    out->activate_text_login_notice = 1;
    out->activation_status = 1;
    out->activation_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->activation_plan_safe = 1;
  out->window_activation_allowed = 1;
  out->window_activation_applied = 0;
  out->activation_error = 0;
  if (queue_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->activate_text_login = 1;
    out->activate_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->activate_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "activation-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (queue_plan->queue_credential_panel && queue_plan->queue_credential_input &&
      queue_plan->queue_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->handoff_envelope = "credential-screen-handoff-envelope";
    out->dispatch_ticket = "credential-screen-dispatch-ticket";
    out->queue_ticket = "credential-screen-queue-ticket";
    out->activation_ticket = "credential-screen-activation-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->activate_credential_panel = 1;
    out->activate_credential_input = 1;
    out->activate_credential_focus = 1;
    out->activate_text_login = 0;
    out->activate_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "activation-credential-ready";
    out->message = "Credential activation ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (queue_plan->queue_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->handoff_envelope = "text-recovery-handoff-envelope";
    out->dispatch_ticket = "text-recovery-dispatch-ticket";
    out->queue_ticket = "text-recovery-queue-ticket";
    out->activation_ticket = "text-recovery-activation-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->activate_text_recovery = 1;
    out->activate_text_login = 1;
    out->activate_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->activate_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "activation-text-recovery-ready";
    out->message = "Text recovery activation ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (queue_plan->queue_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->handoff_envelope = "text-login-resume-handoff-envelope";
    out->dispatch_ticket = "text-login-resume-dispatch-ticket";
    out->queue_ticket = "text-login-resume-queue-ticket";
    out->activation_ticket = "text-login-resume-activation-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->activate_text_login = 1;
    out->activate_text_login_resume = 1;
    out->activate_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->activate_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "activation-resume-ready";
    out->message = "Text login resume activation ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (queue_plan->queue_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->activate_text_login = 1;
    out->activate_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->activate_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "activation-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-activation-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen activation plan blocked; use text login.";
  out->blocked_reason = "credential-activation-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
  out->queue_ticket = "text-login-fallback-queue-ticket";
  out->activation_ticket = "text-login-fallback-activation-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->activate_credential_focus = 0;
  out->activate_text_login = 1;
  out->activate_text_login_fallback = 1;
  out->activation_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}
