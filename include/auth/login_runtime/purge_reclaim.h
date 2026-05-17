#ifndef AUTH_LOGIN_RUNTIME_PURGE_RECLAIM_H
#define AUTH_LOGIN_RUNTIME_PURGE_RECLAIM_H

/*
 * include/auth/login_runtime/purge_reclaim.h
 *
 * Aggregator partial header for the five purge/reclaim plan structs.
 * Each struct lives in its own standalone partial to satisfy the
 * 900-line audit rule; this aggregator preserves the historical
 * include path used by `include/auth/login_runtime.h`.
 *
 *   - struct login_window_credential_screen_purge_plan
 *   - struct login_window_credential_screen_tombstone_plan
 *   - struct login_window_credential_screen_compaction_plan
 *   - struct login_window_credential_screen_reclaim_plan
 *   - struct login_window_credential_screen_release_plan
 *
 * Split performed in PR 11a (per-struct refactor) — see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/purge_plan.h"
#include "auth/login_runtime/tombstone_plan.h"
#include "auth/login_runtime/compaction_plan.h"
#include "auth/login_runtime/reclaim_plan.h"
#include "auth/login_runtime/release_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_PURGE_RECLAIM_H */
