#ifndef AUTH_LOGIN_RUNTIME_WINDOW_DISPLAY_H
#define AUTH_LOGIN_RUNTIME_WINDOW_DISPLAY_H

/*
 * include/auth/login_runtime/window_display.h
 *
 * Aggregator partial header for the five display-pipeline window
 * plan structs (damage tracking, present submission, scheduling,
 * vsync gating, scanout dispatch). Each struct lives in its own
 * standalone partial to satisfy the 900-line audit rule; this
 * aggregator preserves the historical include path used by
 * `include/auth/login_runtime.h`.
 *
 *   - struct login_window_credential_screen_window_damage_plan
 *   - struct login_window_credential_screen_window_present_plan
 *   - struct login_window_credential_screen_window_schedule_plan
 *   - struct login_window_credential_screen_window_vsync_plan
 *   - struct login_window_credential_screen_window_scanout_plan
 *
 * Split performed in PR 11b (per-struct refactor) — see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/window_damage_plan.h"
#include "auth/login_runtime/window_present_plan.h"
#include "auth/login_runtime/window_schedule_plan.h"
#include "auth/login_runtime/window_vsync_plan.h"
#include "auth/login_runtime/window_scanout_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_WINDOW_DISPLAY_H */
