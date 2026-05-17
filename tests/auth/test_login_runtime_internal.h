#ifndef TESTS_AUTH_TEST_LOGIN_RUNTIME_INTERNAL_H
#define TESTS_AUTH_TEST_LOGIN_RUNTIME_INTERNAL_H

/*
 * tests/auth/test_login_runtime_internal.h
 *
 * Internal helpers shared across the split translation units of
 * `tests/auth/test_login_runtime.c` (PR D.0 of the Estagio D
 * dedicated plan).  The main test file keeps the orchestration entry
 * point `run_login_runtime_tests` plus the linker-visible definitions
 * (`shell_build_prompt`, `session_language`) and the shared fixture
 * state.  Companion translation units include this header to consume
 * the fixture state and helpers through external linkage, then export
 * their own `*_cases()` entry that the main file invokes.
 *
 * Linker symbols exposed here (defined in test_login_runtime.c):
 *
 *   - Shared fixture state (g_shell_ctx, g_session_ctx, g_settings,
 *     all counters, readline sequence, printed buffer).
 *   - Shared helpers: reset_test_state, append_printed, strings_equal,
 *     copy_text, expect_true, build_ops.
 *
 * Stubs (prepare_shell_runtime_stub, session_reset_stub, etc.) remain
 * `static` in the main file because they are only referenced through
 * the `login_runtime_ops` vtable returned by `build_ops()`; companion
 * translation units never call them directly.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include <stddef.h>

#include "auth/login_runtime.h"
#include "auth/login_window_gui_layout.h"

/* Shared fixture state (definitions live in test_login_runtime.c). */
extern struct shell_context g_shell_ctx;
extern struct session_context g_session_ctx;
extern struct system_settings g_settings;
extern int g_prepare_calls;
extern int g_maintenance_calls;
extern int g_login_calls;
extern int g_init_user_calls;
extern int g_dispatch_calls;
extern int g_banner_calls;
extern int g_clear_calls;
extern int g_splash_calls;
extern int g_should_logout;
extern int g_runtime_maintenance_active;
extern int g_recovery_login_requested;
extern int g_resume_login_keeps_maintenance;
extern const char *g_readline_sequence[8];
extern size_t g_readline_count;
extern size_t g_readline_index;
extern char g_printed[2048];
extern size_t g_printed_len;

/* Shared helpers (definitions live in test_login_runtime.c). */
void reset_test_state(void);
void append_printed(const char *text);
int strings_equal(const char *a, const char *b);
void copy_text(char *dst, size_t dst_size, const char *src);
int expect_true(int cond, const char *msg);
struct login_runtime_ops build_ops(void);

/* Companion entries (definitions live in test_login_runtime_*.c). */
int test_login_runtime_credential_pre_pipeline_cases(void);
int test_login_runtime_credential_input_view_cases(void);
int test_login_runtime_credential_input_view_panel_cases(void);
int test_login_runtime_credential_audit_view_cases(void);
int test_login_runtime_credential_ui_session_cases(void);
int test_login_runtime_credential_screen_cases(void);
int test_login_runtime_credential_screen_view_model_cases(void);
int test_login_runtime_credential_action_event_cases(void);
int test_login_runtime_credential_route_controller_cases(void);
int test_login_runtime_credential_presenter_binding_cases(void);
int test_login_runtime_credential_mount_commit_cases(void);
int test_login_runtime_credential_handoff_dispatch_cases(void);
int test_login_runtime_credential_queue_activation_cases(void);
int test_login_runtime_credential_frame_surface_cases(void);
int test_login_runtime_credential_compositor_damage_cases(void);
int test_login_runtime_credential_present_plan_cases(void);
int test_login_runtime_credential_schedule_vsync_cases(void);
int test_login_runtime_credential_scanout_display_cases(void);
int test_login_runtime_credential_output_blit_cases(void);
int test_login_runtime_credential_framebuffer_flush_cases(void);
int test_login_runtime_credential_barrier_fence_cases(void);
int test_login_runtime_credential_timeline_sync_cases(void);
int test_login_runtime_credential_deadline_completion_cases(void);
int test_login_runtime_credential_ack_retire_cases(void);
int test_login_runtime_credential_cleanup_seal_cases(void);
int test_login_runtime_credential_audit_record_cases(void);
int test_login_runtime_credential_receipt_ledger_cases(void);
int test_login_runtime_credential_journal_archive_cases(void);
int test_login_runtime_credential_retention_expiry_cases(void);
int test_login_runtime_credential_expiry_plan_cases(void);
int test_login_runtime_credential_purge_cases(void);
int test_login_runtime_credential_tombstone_cases(void);
int test_login_runtime_credential_compaction_reclaim_cases(void);
int test_login_runtime_credential_release_gui_cases(void);
int test_login_runtime_credential_window_surface_cases(void);
int test_login_runtime_credential_window_compositor_damage_cases(void);
int test_login_runtime_credential_window_present_cases(void);
int test_login_runtime_credential_window_schedule_cases(void);
int test_login_runtime_credential_window_vsync_cases(void);
int test_login_runtime_credential_window_scanout_cases(void);
int test_login_runtime_credential_window_display_cases(void);
int test_login_runtime_credential_window_output_cases(void);
int test_login_runtime_credential_window_blit_cases(void);
int test_login_runtime_credential_window_commit_cases(void);
int test_login_runtime_credential_window_flip_cases(void);
int test_login_runtime_credential_window_vblank_cases(void);
int test_login_runtime_credential_window_event_cases(void);
int test_login_runtime_credential_window_input_cases(void);
int test_login_runtime_credential_pipeline_safety_report_cases(void);

/* Shared multi-stage credential screen pipeline helpers. Each
 * companion file in this chain exposes its helper so later stages
 * can build on top of it without duplicating the setup code. */
int build_loginwindow_credential_screen_controller_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_controller *controller);
int build_loginwindow_credential_screen_presenter_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_presenter *presenter);
int build_loginwindow_credential_screen_binding_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_binding *binding);
int build_loginwindow_credential_screen_mount_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_mount_plan *mount_plan);
int build_loginwindow_credential_screen_commit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_commit_plan *commit_plan);
int build_loginwindow_credential_screen_handoff_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_handoff_plan *handoff_plan);
int build_loginwindow_credential_screen_dispatch_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_dispatch_plan *dispatch_plan);
int build_loginwindow_credential_screen_queue_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_queue_plan *queue_plan);
int build_loginwindow_credential_screen_activation_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_activation_plan *activation_plan);
int build_loginwindow_credential_screen_frame_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_frame_plan *frame_plan);
int build_loginwindow_credential_screen_surface_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_surface_plan *surface_plan);
int build_loginwindow_credential_screen_compositor_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_compositor_plan *compositor_plan);
int build_loginwindow_credential_screen_damage_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_damage_plan *damage_plan);
int build_loginwindow_credential_screen_present_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_present_plan *present_plan);
int build_loginwindow_credential_screen_schedule_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_schedule_plan *schedule_plan);
int build_loginwindow_credential_screen_vsync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_vsync_plan *vsync_plan);
int build_loginwindow_credential_screen_scanout_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_scanout_plan *scanout_plan);
int build_loginwindow_credential_screen_display_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_display_plan *display_plan);
int build_loginwindow_credential_screen_output_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_output_plan *output_plan);
int build_loginwindow_credential_screen_blit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_blit_plan *blit_plan);
int build_loginwindow_credential_screen_framebuffer_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_framebuffer_plan *framebuffer_plan);
int build_loginwindow_credential_screen_flush_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_flush_plan *flush_plan);
int build_loginwindow_credential_screen_barrier_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_barrier_plan *barrier_plan);
int build_loginwindow_credential_screen_fence_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_fence_plan *fence_plan);
int build_loginwindow_credential_screen_timeline_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_timeline_plan *timeline_plan);
int build_loginwindow_credential_screen_sync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_sync_plan *sync_plan);
int build_loginwindow_credential_screen_deadline_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_deadline_plan *deadline_plan);
int build_loginwindow_credential_screen_completion_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_completion_plan *completion_plan);
int build_loginwindow_credential_screen_ack_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_ack_plan *ack_plan);
int build_loginwindow_credential_screen_retire_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_retire_plan *retire_plan);
int build_loginwindow_credential_screen_cleanup_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_cleanup_plan *cleanup_plan);
int build_loginwindow_credential_screen_seal_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_seal_plan *seal_plan);
int build_loginwindow_credential_screen_audit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_audit_plan *audit_plan);
int build_loginwindow_credential_screen_record_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_record_plan *record_plan);
int build_loginwindow_credential_screen_receipt_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_receipt_plan *receipt_plan);
int build_loginwindow_credential_screen_ledger_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_ledger_plan *ledger_plan);
int build_loginwindow_credential_screen_journal_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_journal_plan *journal_plan);
int build_loginwindow_credential_screen_archive_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_archive_plan *archive_plan);
int build_loginwindow_credential_screen_retention_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_retention_plan *retention_plan);
int build_loginwindow_credential_screen_expiry_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_expiry_plan *expiry_plan);
int build_loginwindow_credential_screen_purge_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_purge_plan *purge_plan);
int build_loginwindow_credential_screen_tombstone_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_tombstone_plan *tombstone_plan);
int build_loginwindow_credential_screen_compaction_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_compaction_plan *compaction_plan);
int build_loginwindow_credential_screen_reclaim_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_reclaim_plan *reclaim_plan);
int build_loginwindow_credential_screen_release_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_release_plan *release_plan);
int build_loginwindow_credential_screen_gui_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_gui_plan *gui_plan);
int build_loginwindow_credential_screen_window_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage, struct login_window_credential_screen_window_plan *window_plan);
int build_loginwindow_credential_screen_window_surface_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_surface_plan *surface_plan);
int build_loginwindow_credential_screen_window_compositor_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_compositor_plan *compositor_plan);
int build_loginwindow_credential_screen_window_damage_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_damage_plan *damage_plan);
int build_loginwindow_credential_screen_window_present_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_present_plan *present_plan);
int build_loginwindow_credential_screen_window_schedule_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_schedule_plan *schedule_plan);
int build_loginwindow_credential_screen_window_vsync_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_vsync_plan *vsync_plan);
int build_loginwindow_credential_screen_window_scanout_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_scanout_plan *scanout_plan);
int build_loginwindow_credential_screen_window_display_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_display_plan *display_plan);
int build_loginwindow_credential_screen_window_output_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_output_plan *output_plan);
int build_loginwindow_credential_screen_window_blit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_blit_plan *blit_plan);
int build_loginwindow_credential_screen_window_commit_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_commit_plan *commit_plan);
int build_loginwindow_credential_screen_window_flip_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_flip_plan *flip_plan);
int build_loginwindow_credential_screen_window_vblank_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_vblank_plan *vblank_plan);
int build_loginwindow_credential_screen_window_event_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_event_plan *event_plan);
int build_loginwindow_credential_screen_window_input_plan_for_action(
    int requested_action, int input_action, char ch,
    int recovery_session_active, int resume_requested, int maintenance_mode,
    int use_storage,
    struct login_window_credential_screen_window_input_plan *input_plan);

#endif /* TESTS_AUTH_TEST_LOGIN_RUNTIME_INTERNAL_H */
