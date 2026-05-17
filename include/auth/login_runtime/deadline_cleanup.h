#ifndef AUTH_LOGIN_RUNTIME_DEADLINE_CLEANUP_H
#define AUTH_LOGIN_RUNTIME_DEADLINE_CLEANUP_H

/*
 * include/auth/login_runtime/deadline_cleanup.h
 *
 * Aggregator header carrying the five deadline / completion /
 * ack / retire / cleanup plan structs. These close out the sync
 * pipeline by arming deadline timers, reporting completion, acking,
 * retiring resources and cleaning up scratch state.
 *
 * Each struct lives in its own per-struct partial header (extracted
 * during the 2026-05-16 preventive refactor so that no partial
 * header crosses the 900-line audit ceiling):
 *
 *   - struct login_window_credential_screen_deadline_plan
 *       -> auth/login_runtime/deadline_plan.h
 *   - struct login_window_credential_screen_completion_plan
 *       -> auth/login_runtime/completion_plan.h
 *   - struct login_window_credential_screen_ack_plan
 *       -> auth/login_runtime/ack_plan.h
 *   - struct login_window_credential_screen_retire_plan
 *       -> auth/login_runtime/retire_plan.h
 *   - struct login_window_credential_screen_cleanup_plan
 *       -> auth/login_runtime/cleanup_plan.h
 *
 * Original PR B+C+D #8 of the dedicated plan extracted these five
 * structs byte-for-byte from `login_runtime.h`; the 2026-05-16
 * preventive refactor split each struct into its own partial
 * header byte-for-byte. See
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/deadline_plan.h"
#include "auth/login_runtime/completion_plan.h"
#include "auth/login_runtime/ack_plan.h"
#include "auth/login_runtime/retire_plan.h"
#include "auth/login_runtime/cleanup_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_DEADLINE_CLEANUP_H */
