#ifndef AUTH_LOGIN_RUNTIME_SEAL_LEDGER_H
#define AUTH_LOGIN_RUNTIME_SEAL_LEDGER_H

/*
 * include/auth/login_runtime/seal_ledger.h
 *
 * Aggregator partial header for the five seal/audit/record/receipt/
 * ledger plan structs that govern the login screen's audit and
 * persistence trail. Each struct lives in its own standalone partial
 * to satisfy the 900-line audit rule; this aggregator preserves the
 * historical include path used by `include/auth/login_runtime.h`.
 *
 *   - struct login_window_credential_screen_seal_plan
 *   - struct login_window_credential_screen_audit_plan
 *   - struct login_window_credential_screen_record_plan
 *   - struct login_window_credential_screen_receipt_plan
 *   - struct login_window_credential_screen_ledger_plan
 *
 * Split performed in PR 11c (per-struct refactor) — see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime/seal_plan.h"
#include "auth/login_runtime/audit_plan.h"
#include "auth/login_runtime/record_plan.h"
#include "auth/login_runtime/receipt_plan.h"
#include "auth/login_runtime/ledger_plan.h"

#endif /* AUTH_LOGIN_RUNTIME_SEAL_LEDGER_H */
