#ifndef AUTH_LOGIN_RUNTIME_JOURNAL_RETENTION_H
#define AUTH_LOGIN_RUNTIME_JOURNAL_RETENTION_H

/*
 * include/auth/login_runtime/journal_retention.h
 *
 * Aggregator partial header for the four journal/archive/retention/
 * expiry plan structs that drive the login screen's audit-trail
 * lifecycle. Each struct lives in its own standalone partial to
 * satisfy the 900-line audit rule; this aggregator preserves the
 * historical include path used by `include/auth/login_runtime.h`.
 *
 *   - struct login_window_credential_screen_journal_plan
 *   - struct login_window_credential_screen_archive_plan
 *   - struct login_window_credential_screen_retention_plan
 *   - struct login_window_credential_screen_expiry_plan
 *
 * Split performed in PR 11d (per-struct refactor) — see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/journal_plan.h"
#include "auth/login_runtime/archive_plan.h"
#include "auth/login_runtime/retention_plan.h"
#include "auth/login_runtime/expiry_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_JOURNAL_RETENTION_H */
