#ifndef AUTH_LOGIN_RUNTIME_WINDOW_OUTPUT_H
#define AUTH_LOGIN_RUNTIME_WINDOW_OUTPUT_H

/*
 * include/auth/login_runtime/window_output.h
 *
 * Aggregator partial header for the five output-pipeline window plan
 * structs (resolved display plan, per-output framebuffer plan, blit
 * plan, commit plan, flip plan). Each struct lives in its own
 * standalone partial to satisfy the 900-line audit rule; this
 * aggregator preserves the historical include path used by
 * `include/auth/login_runtime.h`.
 *
 *   - struct login_window_credential_screen_window_display_plan
 *   - struct login_window_credential_screen_window_output_plan
 *   - struct login_window_credential_screen_window_blit_plan
 *   - struct login_window_credential_screen_window_commit_plan
 *   - struct login_window_credential_screen_window_flip_plan
 *
 * Split performed in PR 11e (per-struct refactor) — see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/window_display_plan.h"
#include "auth/login_runtime/window_output_plan.h"
#include "auth/login_runtime/window_blit_plan.h"
#include "auth/login_runtime/window_commit_plan.h"
#include "auth/login_runtime/window_flip_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_WINDOW_OUTPUT_H */
