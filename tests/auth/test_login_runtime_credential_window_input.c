/*
 * tests/auth/test_login_runtime_credential_window_input.c
 *
 * Credential screen window input plan coverage for the
 * `login_runtime` host test. Carved out of
 * `tests/auth/test_login_runtime.c` at the 2026-05-16 monolith
 * refactor (PR D.46 of the Estagio D dedicated plan) so each
 * host-test translation unit stays under the 900-line layout
 * limit. Tests in this file exercise:
 *
 *   - `login_window_credential_screen_window_input_plan_build`: 4
 *     tests covering the credential widgets input + the text-route
 *     input (recovery + resume) + the submit/unknown fallback input
 *     + the missing-or-unsafe event plan fail-closed default.
 *
 * Also exposes shared helper
 * `build_loginwindow_credential_screen_window_input_plan_for_action`,
 * used by later companion files that chain on top of the input
 * stage (pipeline_safety_report, ...).
 *
 * The companion entry `test_login_runtime_credential_window_input_cases`
 * is invoked by `run_login_runtime_tests` in
 * `tests/auth/test_login_runtime.c`.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */
#include "test_login_runtime_internal.h"

int build_loginwindow_credential_screen_window_input_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_input_plan *input_plan) {
  struct login_window_credential_screen_window_event_plan event_plan;

  if (build_loginwindow_credential_screen_window_event_plan_for_action(
          requested_action, input_action, ch, recovery_session_active,
          resume_requested, maintenance_mode, use_storage, &event_plan) != 0) {
    return -1;
  }
  return login_window_credential_screen_window_input_plan_build(
      &event_plan, input_plan);
}

static int test_loginwindow_credential_screen_window_input_plan_marks_credential_widgets(void) {
  int fails = 0;
  struct login_window_credential_screen_window_input_plan input_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'p', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential window input plan edit should build");
  fails += expect_true(
      input_plan.version ==
          LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_INPUT_PLAN_VERSION,
      "credential window input plan should expose stable version");
  fails += expect_true(input_plan.window_event_plan_available == 1 &&
                           input_plan.window_vblank_plan_available == 1 &&
                           input_plan.window_flip_plan_available == 1 &&
                           input_plan.window_commit_plan_available == 1 &&
                           input_plan.window_blit_plan_available == 1 &&
                           input_plan.window_output_plan_available == 1 &&
                           input_plan.window_display_plan_available == 1 &&
                           input_plan.window_scanout_plan_available == 1 &&
                           input_plan.window_vsync_plan_available == 1 &&
                           input_plan.window_schedule_plan_available == 1 &&
                           input_plan.window_present_plan_available == 1 &&
                           input_plan.window_damage_plan_available == 1 &&
                           input_plan.window_compositor_plan_available == 1 &&
                           input_plan.window_surface_plan_available == 1 &&
                           input_plan.window_surface_plan_safe == 1 &&
                           input_plan.window_compositor_plan_safe == 1 &&
                           input_plan.window_damage_plan_safe == 1 &&
                           input_plan.window_present_plan_safe == 1 &&
                           input_plan.window_schedule_plan_safe == 1 &&
                           input_plan.window_vsync_plan_safe == 1 &&
                           input_plan.window_scanout_plan_safe == 1 &&
                           input_plan.window_display_plan_safe == 1 &&
                           input_plan.window_output_plan_safe == 1 &&
                           input_plan.window_blit_plan_safe == 1 &&
                           input_plan.window_commit_plan_safe == 1 &&
                           input_plan.window_flip_plan_safe == 1 &&
                           input_plan.window_vblank_plan_safe == 1 &&
                           input_plan.window_event_plan_safe == 1 &&
                           input_plan.window_input_plan_safe == 1,
                       "credential window input plan should require safe event plan");
  fails += expect_true(input_plan.input_required == 1 &&
                           input_plan.input_allowed == 1 &&
                           input_plan.input_submitted == 0 &&
                           input_plan.input_ticket_selected == 1 &&
                           input_plan.input_target_selected == 1 &&
                           input_plan.input_keyboard_armed == 0 &&
                           input_plan.input_keyboard_submitted == 0 &&
                           input_plan.input_pointer_armed == 0 &&
                           input_plan.input_pointer_submitted == 0 &&
                           input_plan.input_focus_armed == 0 &&
                           input_plan.input_focus_submitted == 0 &&
                           input_plan.input_keymap_loaded == 0 &&
                           input_plan.input_keymap_submitted == 0 &&
                           input_plan.input_decode_allowed == 0 &&
                           input_plan.input_decode_submitted == 0 &&
                           input_plan.input_route_allowed == 0 &&
                           input_plan.input_route_submitted == 0 &&
                           input_plan.input_callback_armed == 0 &&
                           input_plan.input_callback_submitted == 0 &&
                           input_plan.input_grab_allowed == 0 &&
                           input_plan.input_grab_submitted == 0 &&
                           input_plan.input_error == 0,
                       "credential window input plan should remain declarative");
  fails += expect_true(input_plan.event_submitted == 0 &&
                           input_plan.event_handler_armed == 0 &&
                           input_plan.event_handler_submitted == 0 &&
                           input_plan.event_queue_armed == 0 &&
                           input_plan.event_queue_submitted == 0 &&
                           input_plan.event_dispatch_allowed == 0 &&
                           input_plan.event_dispatch_submitted == 0 &&
                           input_plan.event_callback_armed == 0 &&
                           input_plan.event_callback_submitted == 0 &&
                           input_plan.event_timestamp_captured == 0 &&
                           input_plan.event_timestamp_submitted == 0 &&
                           input_plan.event_frame_completed == 0 &&
                           input_plan.event_frame_submitted == 0,
                       "credential window input plan must not copy any real upstream effect");
  fails += expect_true(input_plan.input_credential_panel == 1 &&
                           input_plan.input_credential_input == 1 &&
                           input_plan.input_credential_focus == 1 &&
                           input_plan.input_text_login == 0 &&
                           input_plan.input_text_login_fallback == 0,
                       "credential window input plan should mark credential widgets");
  fails += expect_true(input_plan.submit_callback_bound == 0 &&
                           input_plan.auth_callback_bound == 0 &&
                           input_plan.submit_enabled == 0 &&
                           input_plan.auth_attempt_allowed == 0 &&
                           input_plan.credential_redacted == 1 &&
                           input_plan.length_redacted == 1 &&
                           input_plan.raw_secret_exposed == 0 &&
                           input_plan.masked_text_exposed == 0,
                       "credential window input plan must stay redacted and auth-disabled");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "credential-screen-window-input-ticket") &&
          strings_equal(input_plan.event_ticket,
                        "credential-screen-window-event-ticket") &&
          strings_equal(input_plan.input_policy,
                        "incremental-window-input-declarative") &&
          strings_equal(input_plan.state,
                        "window-input-credential-ready"),
      "credential window input plan should report input ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_input_plan_marks_text_routes(void) {
  int fails = 0;
  struct login_window_credential_screen_window_input_plan input_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_OPEN_TEXT_RECOVERY,
          LOGIN_WINDOW_CREDENTIAL_INPUT_CANCEL, 0, 0, 0, 1, 1,
          &input_plan) == 0,
      "credential window input plan recovery should build");
  fails += expect_true(input_plan.window_input_plan_safe == 1 &&
                           input_plan.input_allowed == 1 &&
                           input_plan.input_submitted == 0 &&
                           input_plan.input_text_recovery == 1 &&
                           input_plan.input_text_login == 1 &&
                           input_plan.input_text_login_fallback == 0 &&
                           input_plan.input_credential_focus == 0,
                       "credential window input plan recovery should mark text recovery");
  fails += expect_true(input_plan.input_focus_allowed == 0 &&
                           input_plan.input_keyboard_armed == 0 &&
                           input_plan.input_pointer_armed == 0 &&
                           input_plan.input_focus_armed == 0 &&
                           input_plan.input_keymap_loaded == 0 &&
                           input_plan.input_decode_allowed == 0 &&
                           input_plan.input_route_allowed == 0 &&
                           input_plan.input_callback_armed == 0 &&
                           input_plan.input_grab_allowed == 0 &&
                           input_plan.input_submitted == 0,
                       "credential window input plan recovery should keep input declarative");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-recovery-window-input-ticket") &&
          strings_equal(input_plan.compositor_target,
                        "text-recovery-window-input") &&
          strings_equal(input_plan.blocked_reason, "text-recovery-only"),
      "credential window input plan recovery should report recovery ticket");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_RESUME_TEXT_LOGIN,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'r', 1, 1, 0, 1,
          &input_plan) == 0,
      "credential window input plan resume should build");
  fails += expect_true(input_plan.window_input_plan_safe == 1 &&
                           input_plan.input_text_login_resume == 1 &&
                           input_plan.session_reset_required == 1 &&
                           input_plan.login_screen_rerender_required == 1,
                       "credential window input plan resume should require full rerender input");
  fails += expect_true(input_plan.input_submitted == 0 &&
                           input_plan.input_keyboard_armed == 0 &&
                           input_plan.input_pointer_armed == 0 &&
                           input_plan.input_focus_armed == 0 &&
                           input_plan.input_keymap_loaded == 0 &&
                           input_plan.input_decode_allowed == 0 &&
                           input_plan.input_route_allowed == 0 &&
                           input_plan.input_callback_armed == 0 &&
                           input_plan.input_grab_allowed == 0 &&
                           input_plan.event_submitted == 0 &&
                           input_plan.submit_enabled == 0 &&
                           input_plan.auth_attempt_allowed == 0,
                       "credential window input plan resume must keep GUI auth disabled");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-resume-window-input-ticket") &&
          strings_equal(input_plan.cache_policy,
                        "window-input-cache-bypassed-for-rerender") &&
          strings_equal(input_plan.state, "window-input-resume-ready"),
      "credential window input plan resume should report resume ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_input_plan_falls_back_for_submit_and_unknown_action(void) {
  int fails = 0;
  struct login_window_credential_screen_window_input_plan input_plan;

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_SUBMIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_SUBMIT, 0, 0, 0, 0, 1,
          &input_plan) == 0,
      "credential window input plan submit should build");
  fails += expect_true(input_plan.window_input_plan_safe == 1 &&
                           input_plan.submit_requested == 1 &&
                           input_plan.submit_blocked == 1 &&
                           input_plan.action_allowed == 0 &&
                           input_plan.action_blocked == 1 &&
                           input_plan.input_focus_allowed == 0 &&
                           input_plan.input_text_login == 1 &&
                           input_plan.input_text_login_fallback == 1 &&
                           input_plan.input_submitted == 0 &&
                           input_plan.input_keyboard_armed == 0 &&
                           input_plan.input_callback_submitted == 0,
                       "credential window input plan submit should force text login");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.input_policy,
                        "fallback-window-input-declarative") &&
          strings_equal(input_plan.blocked_reason, "gui-submit-disabled"),
      "credential window input plan submit should report disabled GUI submit");

  fails += expect_true(
      build_loginwindow_credential_screen_window_input_plan_for_action(
          9876, LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'x', 0, 0, 0, 1,
          &input_plan) == 0,
      "credential window input plan unknown action should build");
  fails += expect_true(input_plan.window_input_plan_safe == 1 &&
                           input_plan.action_allowed == 0 &&
                           input_plan.action_blocked == 1 &&
                           input_plan.input_focus_allowed == 0 &&
                           input_plan.input_text_login == 1 &&
                           input_plan.input_text_login_fallback == 1 &&
                           input_plan.input_submitted == 0,
                       "credential window input plan unknown action should force text login");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.compositor_target,
                        "text-login-fallback-window-input") &&
          strings_equal(input_plan.state,
                        "window-input-text-login-ready"),
      "credential window input plan unknown action should report fallback ticket");
  return fails;
}

static int test_loginwindow_credential_screen_window_input_plan_fails_closed_for_unsafe_or_missing_event_plan(void) {
  int fails = 0;
  struct login_window_credential_screen_window_event_plan event_plan;
  struct login_window_credential_screen_window_input_plan input_plan;

  fails += expect_true(
      login_window_credential_screen_window_input_plan_build(
          NULL, &input_plan) == 0,
      "credential window input plan missing event should build fallback");
  fails += expect_true(input_plan.window_event_plan_available == 0 &&
                           input_plan.window_event_plan_safe == 0 &&
                           input_plan.window_input_plan_safe == 0 &&
                           input_plan.route_blocked == 1 &&
                           input_plan.input_allowed == 0 &&
                           input_plan.input_ticket_selected == 0 &&
                           input_plan.input_target_selected == 0 &&
                           input_plan.input_text_login == 1 &&
                           input_plan.input_text_login_fallback == 1 &&
                           input_plan.input_submitted == 0,
                       "credential window input plan missing event should fail closed");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.event_type,
                        "credential-screen-window-input-plan-unavailable") &&
          strings_equal(input_plan.blocked_reason,
                        "window-event-plan-unavailable"),
      "credential window input plan missing event should report missing upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_event_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'u', 0, 0, 0, 1,
          &event_plan) == 0,
      "credential window input plan unsafe event fixture should build");
  event_plan.window_event_plan_safe = 0;
  event_plan.raw_secret_exposed = 1;
  event_plan.submit_blocked = 0;
  fails += expect_true(
      login_window_credential_screen_window_input_plan_build(
          &event_plan, &input_plan) == 0,
      "credential window input plan unsafe event should build fallback");
  fails += expect_true(input_plan.window_event_plan_available == 1 &&
                           input_plan.window_event_plan_safe == 0 &&
                           input_plan.window_input_plan_safe == 0 &&
                           input_plan.route_blocked == 1 &&
                           input_plan.input_allowed == 0 &&
                           input_plan.input_ticket_selected == 0 &&
                           input_plan.input_target_selected == 0 &&
                           input_plan.input_text_login == 1 &&
                           input_plan.input_text_login_fallback == 1 &&
                           input_plan.raw_secret_exposed == 0,
                       "credential window input plan unsafe event should fail closed");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.event_type,
                        "credential-screen-window-input-plan-unsafe") &&
          strings_equal(input_plan.blocked_reason,
                        "credential-window-input-plan-unsafe"),
      "credential window input plan unsafe event should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_event_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 'f', 0, 0, 0, 1,
          &event_plan) == 0,
      "credential window input plan forged origin fixture should build");
  event_plan.window_vblank_plan_available = 0;
  event_plan.window_surface_plan_safe = 0;
  event_plan.window_event_plan_safe = 1;
  fails += expect_true(
      login_window_credential_screen_window_input_plan_build(
          &event_plan, &input_plan) == 0,
      "credential window input plan forged event origin should build fallback");
  fails += expect_true(input_plan.window_event_plan_available == 1 &&
                           input_plan.window_vblank_plan_available == 0 &&
                           input_plan.window_surface_plan_safe == 0 &&
                           input_plan.window_event_plan_safe == 1 &&
                           input_plan.window_input_plan_safe == 0 &&
                           input_plan.input_allowed == 0 &&
                           input_plan.input_ticket_selected == 0 &&
                           input_plan.input_text_login_fallback == 1 &&
                           input_plan.submit_enabled == 0 &&
                           input_plan.auth_attempt_allowed == 0,
                       "credential window input plan should reject forged event origin");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.event_type,
                        "credential-screen-window-input-plan-unsafe") &&
          strings_equal(input_plan.blocked_reason,
                        "credential-window-input-plan-unsafe"),
      "credential window input plan forged origin should report unsafe upstream");

  fails += expect_true(
      build_loginwindow_credential_screen_window_event_plan_for_action(
          LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_EDIT_CREDENTIAL,
          LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND, 's', 0, 0, 0, 1,
          &event_plan) == 0,
      "credential window input plan submitted event fixture should build");
  event_plan.event_submitted = 1;
  event_plan.event_handler_armed = 1;
  event_plan.event_handler_submitted = 1;
  event_plan.event_queue_armed = 1;
  event_plan.event_queue_submitted = 1;
  event_plan.event_dispatch_allowed = 1;
  event_plan.event_dispatch_submitted = 1;
  event_plan.event_callback_armed = 1;
  event_plan.event_callback_submitted = 1;
  event_plan.event_timestamp_captured = 1;
  event_plan.event_timestamp_submitted = 1;
  event_plan.event_frame_completed = 1;
  event_plan.event_frame_submitted = 1;
  fails += expect_true(
      login_window_credential_screen_window_input_plan_build(
          &event_plan, &input_plan) == 0,
      "credential window input plan submitted event should build fallback");
  fails += expect_true(input_plan.window_input_plan_safe == 0 &&
                           input_plan.input_allowed == 0 &&
                           input_plan.input_submitted == 0 &&
                           input_plan.input_keyboard_armed == 0 &&
                           input_plan.input_pointer_armed == 0 &&
                           input_plan.input_focus_armed == 0 &&
                           input_plan.input_keymap_loaded == 0 &&
                           input_plan.input_decode_allowed == 0 &&
                           input_plan.input_route_allowed == 0 &&
                           input_plan.input_callback_armed == 0 &&
                           input_plan.input_grab_allowed == 0 &&
                           input_plan.event_submitted == 0 &&
                           input_plan.event_handler_armed == 0 &&
                           input_plan.event_queue_armed == 0 &&
                           input_plan.event_dispatch_allowed == 0 &&
                           input_plan.event_callback_armed == 0 &&
                           input_plan.event_timestamp_captured == 0 &&
                           input_plan.event_frame_completed == 0 &&
                           input_plan.submit_enabled == 0 &&
                           input_plan.auth_attempt_allowed == 0,
                       "credential window input plan must not copy unsafe submitted event state");
  fails += expect_true(
      strings_equal(input_plan.input_ticket,
                    "text-login-fallback-window-input-ticket") &&
          strings_equal(input_plan.event_type,
                        "credential-screen-window-input-plan-unsafe") &&
          strings_equal(input_plan.blocked_reason,
                        "credential-window-input-plan-unsafe"),
      "credential window input plan submitted event should report unsafe upstream");
  return fails;
}

int test_login_runtime_credential_window_input_cases(void) {
  int fails = 0;
  fails += test_loginwindow_credential_screen_window_input_plan_marks_credential_widgets();
  fails += test_loginwindow_credential_screen_window_input_plan_marks_text_routes();
  fails += test_loginwindow_credential_screen_window_input_plan_falls_back_for_submit_and_unknown_action();
  fails += test_loginwindow_credential_screen_window_input_plan_fails_closed_for_unsafe_or_missing_event_plan();
  return fails;
}
