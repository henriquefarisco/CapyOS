/*
 * src/auth/login_runtime/pipeline_safety.c
 *
 * Credential-screen pipeline-safety report reset + aggregating
 * builder — extracted byte-for-byte from `src/auth/login_runtime.c`
 * during PR C.64 of the Estagio C dedicated plan.  Hosts the
 * pipeline-safety stage that aggregates the entire upstream chain
 * into a single safety report:
 *
 *   - login_window_credential_screen_pipeline_safety_report_reset (static)
 *   - login_window_credential_screen_pipeline_safety_report_build
 *
 * The pipeline-safety-report consumes the terminal window-input
 * plan and produces a fail-closed aggregated report indicating
 * whether the credential pipeline can be considered globally safe
 * for activation.  The static `_reset` helper is the canonical
 * "blocked" initializer used when the upstream contract is missing
 * or unsafe.  Opens Phase 7 of the Estagio C dedicated plan (final
 * cleanup of `login_runtime.c`).
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static void login_window_credential_screen_pipeline_safety_report_reset(
    struct login_window_credential_screen_pipeline_safety_report *out,
    int window_input_plan_available) {
  *out = (struct login_window_credential_screen_pipeline_safety_report){0};
  out->version = LOGIN_WINDOW_CREDENTIAL_SCREEN_PIPELINE_SAFETY_REPORT_VERSION;
  out->window_input_plan_available = window_input_plan_available ? 1 : 0;
  out->total_layers = 15;
  out->layers_safe = 0;
  out->layers_unsafe = 15;
  out->no_real_event_dispatched = 1;
  out->no_real_input_dispatched = 1;
  out->deepest_safe_layer = "none";
  out->first_unsafe_layer = "window_surface_plan";
  out->event_type =
      "credential-screen-pipeline-safety-report-unavailable";
  out->state = "blocked";
  out->blocked_reason = "window-input-plan-unavailable";
  out->message =
      "Pipeline safety report unavailable; text login remains authoritative.";
}

int login_window_credential_screen_pipeline_safety_report_build(
    const struct login_window_credential_screen_window_input_plan *input_plan,
    struct login_window_credential_screen_pipeline_safety_report *out) {
  int safe;
  if (!out) return -1;
  login_window_credential_screen_pipeline_safety_report_reset(
      out, input_plan ? 1 : 0);
  if (!input_plan) return 0;

  out->window_surface_plan_safe =
      input_plan->window_surface_plan_safe ? 1 : 0;
  out->window_compositor_plan_safe =
      input_plan->window_compositor_plan_safe ? 1 : 0;
  out->window_damage_plan_safe =
      input_plan->window_damage_plan_safe ? 1 : 0;
  out->window_present_plan_safe =
      input_plan->window_present_plan_safe ? 1 : 0;
  out->window_schedule_plan_safe =
      input_plan->window_schedule_plan_safe ? 1 : 0;
  out->window_vsync_plan_safe =
      input_plan->window_vsync_plan_safe ? 1 : 0;
  out->window_scanout_plan_safe =
      input_plan->window_scanout_plan_safe ? 1 : 0;
  out->window_display_plan_safe =
      input_plan->window_display_plan_safe ? 1 : 0;
  out->window_output_plan_safe =
      input_plan->window_output_plan_safe ? 1 : 0;
  out->window_blit_plan_safe = input_plan->window_blit_plan_safe ? 1 : 0;
  out->window_commit_plan_safe =
      input_plan->window_commit_plan_safe ? 1 : 0;
  out->window_flip_plan_safe = input_plan->window_flip_plan_safe ? 1 : 0;
  out->window_vblank_plan_safe =
      input_plan->window_vblank_plan_safe ? 1 : 0;
  out->window_event_plan_safe =
      input_plan->window_event_plan_safe ? 1 : 0;
  out->window_input_plan_safe =
      input_plan->window_input_plan_safe ? 1 : 0;

  out->layers_safe = out->window_surface_plan_safe +
                     out->window_compositor_plan_safe +
                     out->window_damage_plan_safe +
                     out->window_present_plan_safe +
                     out->window_schedule_plan_safe +
                     out->window_vsync_plan_safe +
                     out->window_scanout_plan_safe +
                     out->window_display_plan_safe +
                     out->window_output_plan_safe +
                     out->window_blit_plan_safe +
                     out->window_commit_plan_safe +
                     out->window_flip_plan_safe +
                     out->window_vblank_plan_safe +
                     out->window_event_plan_safe +
                     out->window_input_plan_safe;
  out->layers_unsafe = out->total_layers - out->layers_safe;

  out->gui_submit_blocked =
      (input_plan->submit_blocked && !input_plan->submit_enabled &&
       !input_plan->submit_callback_bound)
          ? 1
          : 0;
  out->auth_attempt_blocked =
      (!input_plan->auth_attempt_allowed &&
       !input_plan->auth_callback_bound)
          ? 1
          : 0;
  out->credentials_redacted =
      (input_plan->credential_redacted && input_plan->length_redacted &&
       !input_plan->raw_secret_exposed && !input_plan->masked_text_exposed)
          ? 1
          : 0;
  out->credentials_storage_wiped =
      input_plan->credential_storage_wiped ? 1 : 0;
  out->text_login_authoritative =
      input_plan->text_login_authoritative ? 1 : 0;
  out->route_consistent =
      (((input_plan->route_selected ? 1 : 0) ^
        (input_plan->route_blocked ? 1 : 0)) != 0)
          ? 1
          : 0;

  out->no_real_event_dispatched =
      (!input_plan->event_submitted && !input_plan->event_handler_armed &&
       !input_plan->event_handler_submitted &&
       !input_plan->event_queue_armed &&
       !input_plan->event_queue_submitted &&
       !input_plan->event_dispatch_submitted &&
       !input_plan->event_callback_armed &&
       !input_plan->event_callback_submitted &&
       !input_plan->event_timestamp_captured &&
       !input_plan->event_timestamp_submitted &&
       !input_plan->event_frame_completed &&
       !input_plan->event_frame_submitted && !input_plan->event_error)
          ? 1
          : 0;
  out->no_real_input_dispatched =
      (!input_plan->input_submitted && !input_plan->input_keyboard_armed &&
       !input_plan->input_keyboard_submitted &&
       !input_plan->input_pointer_armed &&
       !input_plan->input_pointer_submitted &&
       !input_plan->input_focus_armed &&
       !input_plan->input_focus_submitted &&
       !input_plan->input_keymap_loaded &&
       !input_plan->input_keymap_submitted &&
       !input_plan->input_decode_submitted &&
       !input_plan->input_route_submitted &&
       !input_plan->input_callback_armed &&
       !input_plan->input_callback_submitted &&
       !input_plan->input_grab_allowed &&
       !input_plan->input_grab_submitted && !input_plan->input_error)
          ? 1
          : 0;

  if (!out->window_surface_plan_safe) {
    out->deepest_safe_layer = "none";
    out->first_unsafe_layer = "window_surface_plan";
  } else if (!out->window_compositor_plan_safe) {
    out->deepest_safe_layer = "window_surface_plan";
    out->first_unsafe_layer = "window_compositor_plan";
  } else if (!out->window_damage_plan_safe) {
    out->deepest_safe_layer = "window_compositor_plan";
    out->first_unsafe_layer = "window_damage_plan";
  } else if (!out->window_present_plan_safe) {
    out->deepest_safe_layer = "window_damage_plan";
    out->first_unsafe_layer = "window_present_plan";
  } else if (!out->window_schedule_plan_safe) {
    out->deepest_safe_layer = "window_present_plan";
    out->first_unsafe_layer = "window_schedule_plan";
  } else if (!out->window_vsync_plan_safe) {
    out->deepest_safe_layer = "window_schedule_plan";
    out->first_unsafe_layer = "window_vsync_plan";
  } else if (!out->window_scanout_plan_safe) {
    out->deepest_safe_layer = "window_vsync_plan";
    out->first_unsafe_layer = "window_scanout_plan";
  } else if (!out->window_display_plan_safe) {
    out->deepest_safe_layer = "window_scanout_plan";
    out->first_unsafe_layer = "window_display_plan";
  } else if (!out->window_output_plan_safe) {
    out->deepest_safe_layer = "window_display_plan";
    out->first_unsafe_layer = "window_output_plan";
  } else if (!out->window_blit_plan_safe) {
    out->deepest_safe_layer = "window_output_plan";
    out->first_unsafe_layer = "window_blit_plan";
  } else if (!out->window_commit_plan_safe) {
    out->deepest_safe_layer = "window_blit_plan";
    out->first_unsafe_layer = "window_commit_plan";
  } else if (!out->window_flip_plan_safe) {
    out->deepest_safe_layer = "window_commit_plan";
    out->first_unsafe_layer = "window_flip_plan";
  } else if (!out->window_vblank_plan_safe) {
    out->deepest_safe_layer = "window_flip_plan";
    out->first_unsafe_layer = "window_vblank_plan";
  } else if (!out->window_event_plan_safe) {
    out->deepest_safe_layer = "window_vblank_plan";
    out->first_unsafe_layer = "window_event_plan";
  } else if (!out->window_input_plan_safe) {
    out->deepest_safe_layer = "window_event_plan";
    out->first_unsafe_layer = "window_input_plan";
  } else {
    out->deepest_safe_layer = "window_input_plan";
    out->first_unsafe_layer = "none";
  }

  safe = (out->layers_safe == out->total_layers) &&
         out->gui_submit_blocked && out->auth_attempt_blocked &&
         out->credentials_redacted && out->credentials_storage_wiped &&
         out->text_login_authoritative && out->route_consistent &&
         out->no_real_event_dispatched && out->no_real_input_dispatched;
  out->pipeline_safe = safe ? 1 : 0;

  if (out->pipeline_safe) {
    out->event_type = "credential-screen-pipeline-safety-report-ready";
    out->state = "pipeline-safe";
    out->blocked_reason = "ready";
    out->message =
        "Loginwindow credential pipeline safety verified across 15 window "
        "layers; graphical authentication remains disabled.";
  } else {
    out->event_type = "credential-screen-pipeline-safety-report-unsafe";
    out->state = "pipeline-unsafe";
    out->blocked_reason = "pipeline-safety-violated";
    out->message =
        "Loginwindow credential pipeline safety violated; use text login.";
  }
  return 0;
}
