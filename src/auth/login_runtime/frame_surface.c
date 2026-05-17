/*
 * src/auth/login_runtime/frame_surface.c
 *
 * Credential-screen frame + surface plan builders — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.13 of
 * the Estagio C dedicated plan.  Hosts the two contiguous pipeline
 * builders that turn an activated credential screen into a frame
 * with allocated drawing surfaces:
 *
 *   - login_window_credential_screen_frame_plan_build
 *   - login_window_credential_screen_surface_plan_build
 *
 * The frame-plan owns the per-frame lifecycle (allocation, identity,
 * timing); the surface-plan owns the GPU/CPU surface bindings that
 * back the frame.  Both are fail-closed.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_credential_screen_frame_plan_build(
    const struct login_window_credential_screen_activation_plan *activation_plan,
    struct login_window_credential_screen_frame_plan *out) {
  int activation_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_FRAME_PLAN_VERSION;
  out->activation_plan_available = activation_plan ? 1 : 0;
  out->activation_plan_safe = 0;
  out->frame_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_frame_required = 1;
  out->window_frame_allowed = 0;
  out->window_frame_rendered = 0;
  out->frame_ticket_selected = 0;
  out->frame_credential_panel = 0;
  out->frame_credential_input = 0;
  out->frame_credential_focus = 0;
  out->frame_text_recovery = 0;
  out->frame_text_login = 1;
  out->frame_text_login_resume = 0;
  out->frame_text_login_fallback = 1;
  out->frame_fallback_notice = 1;
  out->frame_text_login_notice = 1;
  out->frame_status = 1;
  out->frame_error = 1;
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
  out->frame_ticket = "text-login-fallback-frame-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->event_type = "credential-screen-frame-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "activation-plan-unavailable";
  if (!activation_plan) return 0;
  out->requested_action = activation_plan->requested_action;
  out->activation_plan_safe = activation_plan->activation_plan_safe ? 1 : 0;
  out->route_selected = activation_plan->route_selected ? 1 : 0;
  out->route_blocked = activation_plan->route_blocked ? 1 : 0;
  out->action_allowed = activation_plan->action_allowed ? 1 : 0;
  out->action_blocked = activation_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = activation_plan->input_focus_allowed ? 1 : 0;
  out->frame_ticket_selected = activation_plan->activation_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = activation_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = activation_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = activation_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = activation_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = activation_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = activation_plan->credential_redacted ? 1 : 0;
  out->length_redacted = activation_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = activation_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = activation_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = activation_plan->submit_requested ? 1 : 0;
  out->submit_blocked = 1;
  out->submit_enabled = 0;
  out->auth_attempt_allowed = 0;
  out->text_login_authoritative = activation_plan->text_login_authoritative ? 1 : 0;
  out->view = activation_plan->view ? activation_plan->view : "text-login-fallback";
  out->widget_tree = activation_plan->widget_tree ? activation_plan->widget_tree
                                                  : "text-login-fallback-bindings";
  out->mount_transaction = activation_plan->mount_transaction ? activation_plan->mount_transaction
                                                              : "text-login-fallback-mount-plan";
  out->commit_transaction = activation_plan->commit_transaction ? activation_plan->commit_transaction
                                                                : "text-login-fallback-commit-plan";
  out->handoff_envelope = activation_plan->handoff_envelope ? activation_plan->handoff_envelope
                                                            : "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = activation_plan->dispatch_ticket ? activation_plan->dispatch_ticket
                                                          : "text-login-fallback-dispatch-ticket";
  out->queue_ticket = activation_plan->queue_ticket ? activation_plan->queue_ticket
                                                    : "text-login-fallback-queue-ticket";
  out->activation_ticket = activation_plan->activation_ticket ? activation_plan->activation_ticket
                                                              : "text-login-fallback-activation-ticket";
  out->focus_target = activation_plan->focus_target ? activation_plan->focus_target : "none";
  out->primary_action = activation_plan->primary_action ? activation_plan->primary_action
                                                        : "use-text-login";
  out->route = activation_plan->route ? activation_plan->route : "force-text-login";
  out->event_type = activation_plan->event_type ? activation_plan->event_type
                                                : "credential-screen-activation-plan-blocked";
  out->state = activation_plan->state ? activation_plan->state : "blocked";
  out->message = activation_plan->message ? activation_plan->message
                                          : "Text login remains authoritative.";
  out->blocked_reason = activation_plan->blocked_reason ? activation_plan->blocked_reason
                                                        : "blocked";
  activation_safe = out->activation_plan_safe && activation_plan->window_activation_allowed &&
                    !activation_plan->window_activation_applied &&
                    out->frame_ticket_selected && out->route_selected &&
                    !out->route_blocked && out->credential_session_safe &&
                    out->credential_storage_wiped && out->credential_redacted &&
                    out->length_redacted && !out->raw_secret_exposed &&
                    !out->masked_text_exposed && activation_plan->submit_blocked &&
                    !activation_plan->submit_enabled && !activation_plan->auth_attempt_allowed &&
                    !activation_plan->submit_callback_bound &&
                    !activation_plan->auth_callback_bound && out->text_login_authoritative;
  if (!activation_safe) {
    out->event_type = "credential-screen-frame-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen frame plan unsafe; use text login.";
    out->blocked_reason = "credential-frame-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_frame_allowed = 0;
    out->window_frame_rendered = 0;
    out->frame_ticket_selected = 0;
    out->frame_credential_panel = 0;
    out->frame_credential_input = 0;
    out->frame_credential_focus = 0;
    out->frame_text_recovery = 0;
    out->frame_text_login = 1;
    out->frame_text_login_resume = 0;
    out->frame_text_login_fallback = 1;
    out->frame_fallback_notice = 1;
    out->frame_text_login_notice = 1;
    out->frame_status = 1;
    out->frame_error = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->frame_plan_safe = 1;
  out->window_frame_allowed = 1;
  out->window_frame_rendered = 0;
  out->frame_error = 0;
  if (activation_plan->submit_requested) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->frame_text_login = 1;
    out->frame_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->frame_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "frame-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (activation_plan->activate_credential_panel && activation_plan->activate_credential_input &&
      activation_plan->activate_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->mount_transaction = "credential-screen-mount-plan";
    out->commit_transaction = "credential-screen-commit-plan";
    out->handoff_envelope = "credential-screen-handoff-envelope";
    out->dispatch_ticket = "credential-screen-dispatch-ticket";
    out->queue_ticket = "credential-screen-queue-ticket";
    out->activation_ticket = "credential-screen-activation-ticket";
    out->frame_ticket = "credential-screen-frame-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->frame_credential_panel = 1;
    out->frame_credential_input = 1;
    out->frame_credential_focus = 1;
    out->frame_text_login = 0;
    out->frame_text_login_fallback = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "frame-credential-ready";
    out->message = "Credential frame ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (activation_plan->activate_text_recovery && out->recovery_text_session_required) {
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->mount_transaction = "text-recovery-mount-plan";
    out->commit_transaction = "text-recovery-commit-plan";
    out->handoff_envelope = "text-recovery-handoff-envelope";
    out->dispatch_ticket = "text-recovery-dispatch-ticket";
    out->queue_ticket = "text-recovery-queue-ticket";
    out->activation_ticket = "text-recovery-activation-ticket";
    out->frame_ticket = "text-recovery-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->frame_text_recovery = 1;
    out->frame_text_login = 1;
    out->frame_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->frame_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "frame-text-recovery-ready";
    out->message = "Text recovery frame ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (activation_plan->activate_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->mount_transaction = "text-login-resume-mount-plan";
    out->commit_transaction = "text-login-resume-commit-plan";
    out->handoff_envelope = "text-login-resume-handoff-envelope";
    out->dispatch_ticket = "text-login-resume-dispatch-ticket";
    out->queue_ticket = "text-login-resume-queue-ticket";
    out->activation_ticket = "text-login-resume-activation-ticket";
    out->frame_ticket = "text-login-resume-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->frame_text_login = 1;
    out->frame_text_login_resume = 1;
    out->frame_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->frame_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "frame-resume-ready";
    out->message = "Text login resume frame ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (activation_plan->activate_text_login_fallback) {
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->mount_transaction = "text-login-fallback-mount-plan";
    out->commit_transaction = "text-login-fallback-commit-plan";
    out->handoff_envelope = "text-login-fallback-handoff-envelope";
    out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
    out->queue_ticket = "text-login-fallback-queue-ticket";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->frame_text_login = 1;
    out->frame_text_login_fallback = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->frame_credential_focus = 0;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->state = "frame-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-frame-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen frame plan blocked; use text login.";
  out->blocked_reason = "credential-frame-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->mount_transaction = "text-login-fallback-mount-plan";
  out->commit_transaction = "text-login-fallback-commit-plan";
  out->handoff_envelope = "text-login-fallback-handoff-envelope";
  out->dispatch_ticket = "text-login-fallback-dispatch-ticket";
  out->queue_ticket = "text-login-fallback-queue-ticket";
  out->activation_ticket = "text-login-fallback-activation-ticket";
  out->frame_ticket = "text-login-fallback-frame-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->frame_credential_focus = 0;
  out->frame_text_login = 1;
  out->frame_text_login_fallback = 1;
  out->frame_error = 1;
  out->submit_callback_bound = 0;
  out->auth_callback_bound = 0;
  return 0;
}

int login_window_credential_screen_surface_plan_build(
    const struct login_window_credential_screen_frame_plan *frame_plan,
    struct login_window_credential_screen_surface_plan *out) {
  int frame_safe = 0;
  if (!out) return -1;
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_SURFACE_PLAN_VERSION;
  out->frame_plan_available = frame_plan ? 1 : 0;
  out->frame_plan_safe = 0;
  out->surface_plan_safe = 0;
  out->requested_action = 0;
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->window_surface_required = 1;
  out->window_surface_allowed = 0;
  out->window_surface_submitted = 0;
  out->compositor_damage_planned = 0;
  out->compositor_damage_submitted = 0;
  out->surface_ticket_selected = 0;
  out->surface_credential_panel = 0;
  out->surface_credential_input = 0;
  out->surface_credential_focus = 0;
  out->surface_text_recovery = 0;
  out->surface_text_login = 1;
  out->surface_text_login_resume = 0;
  out->surface_text_login_fallback = 1;
  out->surface_error = 1;
  out->surface_reuse_allowed = 0;
  out->surface_cache_allowed = 0;
  out->full_damage_required = 1;
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
  out->activation_ticket = "text-login-fallback-activation-ticket";
  out->frame_ticket = "text-login-fallback-frame-ticket";
  out->surface_ticket = "text-login-fallback-surface-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->event_type = "credential-screen-surface-plan-unavailable";
  out->state = "blocked";
  out->message = "Text login remains authoritative.";
  out->blocked_reason = "frame-plan-unavailable";
  if (!frame_plan) return 0;
  out->requested_action = frame_plan->requested_action;
  out->frame_plan_safe = frame_plan->frame_plan_safe ? 1 : 0;
  out->route_selected = frame_plan->route_selected ? 1 : 0;
  out->route_blocked = frame_plan->route_blocked ? 1 : 0;
  out->action_allowed = frame_plan->action_allowed ? 1 : 0;
  out->action_blocked = frame_plan->action_blocked ? 1 : 0;
  out->input_focus_allowed = frame_plan->input_focus_allowed ? 1 : 0;
  out->surface_ticket_selected = frame_plan->frame_ticket_selected ? 1 : 0;
  out->recovery_text_session_required = frame_plan->recovery_text_session_required ? 1 : 0;
  out->session_reset_required = frame_plan->session_reset_required ? 1 : 0;
  out->login_screen_rerender_required = frame_plan->login_screen_rerender_required ? 1 : 0;
  out->credential_session_safe = frame_plan->credential_session_safe ? 1 : 0;
  out->credential_storage_wiped = frame_plan->credential_storage_wiped ? 1 : 0;
  out->credential_redacted = frame_plan->credential_redacted ? 1 : 0;
  out->length_redacted = frame_plan->length_redacted ? 1 : 0;
  out->raw_secret_exposed = frame_plan->raw_secret_exposed ? 1 : 0;
  out->masked_text_exposed = frame_plan->masked_text_exposed ? 1 : 0;
  out->submit_requested = frame_plan->submit_requested ? 1 : 0;
  out->text_login_authoritative = frame_plan->text_login_authoritative ? 1 : 0;
  out->view = frame_plan->view ? frame_plan->view : "text-login-fallback";
  out->widget_tree = frame_plan->widget_tree ? frame_plan->widget_tree
                                             : "text-login-fallback-bindings";
  out->activation_ticket = frame_plan->activation_ticket ? frame_plan->activation_ticket
                                                         : "text-login-fallback-activation-ticket";
  out->frame_ticket = frame_plan->frame_ticket ? frame_plan->frame_ticket
                                               : "text-login-fallback-frame-ticket";
  out->focus_target = frame_plan->focus_target ? frame_plan->focus_target : "none";
  out->primary_action = frame_plan->primary_action ? frame_plan->primary_action
                                                   : "use-text-login";
  out->route = frame_plan->route ? frame_plan->route : "force-text-login";
  out->event_type = frame_plan->event_type ? frame_plan->event_type
                                           : "credential-screen-frame-plan-blocked";
  out->state = frame_plan->state ? frame_plan->state : "blocked";
  out->message = frame_plan->message ? frame_plan->message
                                     : "Text login remains authoritative.";
  out->blocked_reason = frame_plan->blocked_reason ? frame_plan->blocked_reason
                                                   : "blocked";
  frame_safe = out->frame_plan_safe && frame_plan->window_frame_allowed &&
               !frame_plan->window_frame_rendered && out->surface_ticket_selected &&
               out->route_selected && !out->route_blocked &&
               out->credential_session_safe && out->credential_storage_wiped &&
               out->credential_redacted && out->length_redacted &&
               !out->raw_secret_exposed && !out->masked_text_exposed &&
               frame_plan->submit_blocked && !frame_plan->submit_enabled &&
               !frame_plan->auth_attempt_allowed && !frame_plan->submit_callback_bound &&
               !frame_plan->auth_callback_bound && out->text_login_authoritative;
  if (!frame_safe) {
    out->event_type = "credential-screen-surface-plan-unsafe";
    out->state = "blocked";
    out->message = "Credential screen surface plan unsafe; use text login.";
    out->blocked_reason = "credential-surface-plan-unsafe";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->activation_ticket = "text-login-fallback-activation-ticket";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->compositor_target = "none";
    out->damage_policy = "full-safe-fallback";
    out->cache_policy = "cache-disabled";
    out->route_selected = 0;
    out->route_blocked = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->window_surface_allowed = 0;
    out->window_surface_submitted = 0;
    out->compositor_damage_planned = 0;
    out->compositor_damage_submitted = 0;
    out->surface_ticket_selected = 0;
    out->surface_credential_panel = 0;
    out->surface_credential_input = 0;
    out->surface_credential_focus = 0;
    out->surface_text_recovery = 0;
    out->surface_text_login = 1;
    out->surface_text_login_resume = 0;
    out->surface_text_login_fallback = 1;
    out->surface_error = 1;
    out->surface_reuse_allowed = 0;
    out->surface_cache_allowed = 0;
    out->full_damage_required = 1;
    out->submit_callback_bound = 0;
    out->auth_callback_bound = 0;
    out->submit_blocked = 1;
    out->submit_enabled = 0;
    out->auth_attempt_allowed = 0;
    return 0;
  }
  out->surface_plan_safe = 1;
  out->window_surface_allowed = 1;
  out->window_surface_submitted = 0;
  out->compositor_damage_planned = 1;
  out->compositor_damage_submitted = 0;
  out->surface_error = 0;
  out->surface_reuse_allowed = 1;
  out->surface_cache_allowed = 1;
  out->full_damage_required = 0;
  out->compositor_target = "loginwindow-compositor-surface";
  out->damage_policy = "incremental-declarative";
  out->cache_policy = "surface-cache-eligible";
  if (frame_plan->submit_requested) {
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->compositor_target = "text-login-fallback-surface";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->surface_text_login = 1;
    out->surface_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->surface_credential_focus = 0;
    out->state = "surface-text-login-ready";
    out->message = "Graphical submit is disabled; use text login.";
    out->blocked_reason = "gui-submit-disabled";
    return 0;
  }
  if (frame_plan->frame_credential_panel && frame_plan->frame_credential_input &&
      frame_plan->frame_credential_focus && out->action_allowed &&
      !out->action_blocked && out->input_focus_allowed) {
    out->surface_ticket = "credential-screen-surface-ticket";
    out->compositor_target = "credential-screen-surface";
    out->view = "credential-screen";
    out->widget_tree = "credential-screen-bindings";
    out->frame_ticket = "credential-screen-frame-ticket";
    out->focus_target = "credential-input";
    out->primary_action = "edit-credential";
    out->surface_credential_panel = 1;
    out->surface_credential_input = 1;
    out->surface_credential_focus = 1;
    out->surface_text_login = 0;
    out->surface_text_login_fallback = 0;
    out->state = "surface-credential-ready";
    out->message = "Credential surface ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (frame_plan->frame_text_recovery && out->recovery_text_session_required) {
    out->surface_ticket = "text-recovery-surface-ticket";
    out->compositor_target = "text-recovery-surface";
    out->view = "text-recovery";
    out->widget_tree = "text-recovery-bindings";
    out->frame_ticket = "text-recovery-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "open-text-recovery";
    out->surface_text_recovery = 1;
    out->surface_text_login = 1;
    out->surface_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->surface_credential_focus = 0;
    out->state = "surface-text-recovery-ready";
    out->message = "Text recovery surface ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "text-recovery-only";
    return 0;
  }
  if (frame_plan->frame_text_login_resume && out->session_reset_required &&
      out->login_screen_rerender_required) {
    out->surface_ticket = "text-login-resume-surface-ticket";
    out->compositor_target = "text-login-resume-surface";
    out->damage_policy = "full-rerender-declarative";
    out->cache_policy = "cache-bypassed-for-rerender";
    out->surface_reuse_allowed = 0;
    out->full_damage_required = 1;
    out->view = "text-login";
    out->widget_tree = "text-login-resume-bindings";
    out->frame_ticket = "text-login-resume-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "resume-text-login";
    out->surface_text_login = 1;
    out->surface_text_login_resume = 1;
    out->surface_text_login_fallback = 0;
    out->input_focus_allowed = 0;
    out->surface_credential_focus = 0;
    out->state = "surface-resume-ready";
    out->message = "Text login resume surface ticket ready; graphical authentication remains disabled.";
    out->blocked_reason = "ready";
    return 0;
  }
  if (frame_plan->frame_text_login_fallback) {
    out->surface_ticket = "text-login-fallback-surface-ticket";
    out->compositor_target = "text-login-fallback-surface";
    out->damage_policy = "fallback-declarative";
    out->view = "text-login-fallback";
    out->widget_tree = "text-login-fallback-bindings";
    out->frame_ticket = "text-login-fallback-frame-ticket";
    out->focus_target = "none";
    out->primary_action = "use-text-login";
    out->route = "force-text-login";
    out->surface_text_login = 1;
    out->surface_text_login_fallback = 1;
    out->full_damage_required = 1;
    out->action_allowed = 0;
    out->action_blocked = 1;
    out->input_focus_allowed = 0;
    out->surface_credential_focus = 0;
    out->state = "surface-text-login-ready";
    out->message = "Use authoritative text login.";
    return 0;
  }
  out->event_type = "credential-screen-surface-plan-blocked";
  out->state = "blocked";
  out->message = "Credential screen surface plan blocked; use text login.";
  out->blocked_reason = "credential-surface-plan-blocked";
  out->view = "text-login-fallback";
  out->widget_tree = "text-login-fallback-bindings";
  out->frame_ticket = "text-login-fallback-frame-ticket";
  out->surface_ticket = "text-login-fallback-surface-ticket";
  out->focus_target = "none";
  out->primary_action = "use-text-login";
  out->route = "force-text-login";
  out->compositor_target = "none";
  out->damage_policy = "full-safe-fallback";
  out->cache_policy = "cache-disabled";
  out->route_selected = 0;
  out->route_blocked = 1;
  out->action_allowed = 0;
  out->action_blocked = 1;
  out->input_focus_allowed = 0;
  out->surface_credential_focus = 0;
  out->surface_text_login = 1;
  out->surface_text_login_fallback = 1;
  out->surface_error = 1;
  out->surface_reuse_allowed = 0;
  out->surface_cache_allowed = 0;
  out->compositor_damage_planned = 0;
  out->full_damage_required = 1;
  return 0;
}
