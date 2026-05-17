#ifndef CORE_LOGIN_RUNTIME_H
#define CORE_LOGIN_RUNTIME_H

#include <stddef.h>

#include "auth/session.h"
#include "core/system_init.h"
#include "auth/user.h"
#include "shell/core.h"

/* Public preprocessor constants (ABI version stamps, password
 * limits, input/screen action codes) live in a dedicated partial
 * header so the facade stays focused on type definitions and
 * declarations.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 12 add-on outside the original 10 PR plan).
 */
#include "auth/login_runtime/version_constants.h"

/* Authentication core structs (recovery resume policy, credential
 * policy/buffer, submit gate/attempt/auth_submit + authenticator
 * callback typedef) live in a partial header. They feed every
 * higher pipeline stage and are pulled in transitively by
 * `pipeline_contract.h` for the value-typed
 * `login_window_credential_policy` field of
 * `struct login_window_view_model`.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 2).
 */
#include "auth/login_runtime/auth_core.h"

/* Input layer (input_result + field_view + credential_panel +
 * interaction + readiness) and audit/view-model layer (audit_event +
 * view_model + ui_step + ui_session) live in dedicated partial
 * headers. They are the next stages above auth_core and feed the
 * recovery_screen pipeline above them.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 3).
 */
#include "auth/login_runtime/input_layer.h"
#include "auth/login_runtime/audit_view.h"

/* Recovery + screen view-model structs (recovery_view_model,
 * screen_view_model, screen_session, screen_render_plan) live in
 * a dedicated partial header. They sit one stage above audit_view
 * and feed the action_route + controller pipelines.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 4).
 */
#include "auth/login_runtime/recovery_screen.h"

/* Action / UI event / route layer (action_plan + ui_event +
 * route_plan), controller / recovery decision / session handoff layer
 * (controller + recovery_decision + session_handoff), and presenter /
 * binding / mount layer (presenter + binding + mount_plan) live in
 * dedicated partial headers.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 5).
 */
#include "auth/login_runtime/action_route.h"
#include "auth/login_runtime/controller_handoff.h"
#include "auth/login_runtime/presenter_mount.h"

/* Commit / handoff / dispatch / queue / activation window-transaction
 * layer + frame / surface / compositor / damage layer live in
 * dedicated partial headers.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 6).
 */
#include "auth/login_runtime/commit_dispatch.h"
#include "auth/login_runtime/frame_compositor.h"

/* Present / schedule / vsync / scanout layer + display / output /
 * blit / framebuffer layer + flush / barrier / fence / timeline /
 * sync layer live in dedicated partial headers.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 7).
 */
#include "auth/login_runtime/present_scanout.h"
#include "auth/login_runtime/display_blit.h"
#include "auth/login_runtime/flush_sync.h"

/* Deadline / completion / ack / retire / cleanup layer + seal /
 * audit / record / receipt / ledger layer live in dedicated partial
 * headers.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 8).
 */
#include "auth/login_runtime/deadline_cleanup.h"
#include "auth/login_runtime/seal_ledger.h"

/* Journal / archive / retention / expiry plan structs (PR 9a). */
#include "auth/login_runtime/journal_retention.h"

/* Purge / tombstone / compaction / reclaim / release plan structs (PR 9b). */
#include "auth/login_runtime/purge_reclaim.h"

/* GUI / window / window_surface / window_compositor plan structs (PR 10a). */
#include "auth/login_runtime/gui_window.h"

/* Display pipeline (damage/present/schedule/vsync/scanout) plan structs (PR 10b). */
#include "auth/login_runtime/window_display.h"

/* Output pipeline (display/output/blit/commit/flip) plan structs (PR 10c). */
#include "auth/login_runtime/window_output.h"

/* Terminal stage (vblank/event/input) plan structs (PR 10d). */
#include "auth/login_runtime/window_input.h"

/* Top-of-pipeline structures (pipeline safety report, login window
 * contract, login window view model, login_runtime_ops) live in a
 * partial header to keep this facade under control. The include must
 * appear AFTER `struct login_window_credential_policy` because
 * `struct login_window_view_model` carries that struct by value.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 1).
 */
#include "auth/login_runtime/pipeline_contract.h"

/* Function declarations for the full login runtime pipeline live in
 * a dedicated partial header so the facade stays focused on type
 * definitions. The include must appear AFTER every struct partial
 * header so the compiler has seen every referenced type when it
 * parses the prototypes. As of PR 10d every struct also lives in a
 * partial header, so this include sits at the very end of the
 * facade right before the closing guard.
 *
 * Tracked in `docs/plans/active/monolith-residual-dedicated-plan.md`
 * (Estagio B+C+D, PR 11 add-on outside the original 10 PR plan).
 */
#include "auth/login_runtime/function_declarations.h"

#endif /* CORE_LOGIN_RUNTIME_H */
