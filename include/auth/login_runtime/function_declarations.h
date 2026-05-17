#ifndef AUTH_LOGIN_RUNTIME_FUNCTION_DECLARATIONS_H
#define AUTH_LOGIN_RUNTIME_FUNCTION_DECLARATIONS_H

/*
 * include/auth/login_runtime/function_declarations.h
 *
 * Function prototypes for the login runtime public API. Contains the
 * full declaration of the pipeline build chain (contract -> view ->
 * input -> auth -> screen -> action/route/controller -> presenter ->
 * mount -> commit/dispatch/queue/activation -> frame/surface/
 * compositor/damage -> present/schedule/vsync/scanout ->
 * display/output/blit/framebuffer -> flush/barrier/fence/timeline/
 * sync -> deadline/completion/ack/retire/cleanup ->
 * seal/audit/record/receipt/ledger -> journal/archive/retention/
 * expiry -> purge/tombstone/compaction/reclaim/release ->
 * gui/window/window_surface/window_compositor/window_damage/
 * window_present/window_schedule/window_vsync/window_scanout/
 * window_display/window_output/window_blit/window_commit/window_flip/
 * window_vblank/window_event/window_input -> pipeline_safety_report)
 * plus the top-level `login_window_view_model_build` and
 * `login_runtime_run` entry points.
 *
 * Inclusion contract: this header is included BY `auth/login_runtime.h`
 * AFTER all the struct partial headers and inline struct definitions
 * so every referenced type is fully visible when the compiler parses
 * these prototypes. Direct inclusion outside of `auth/login_runtime.h`
 * is not supported; callers should include `auth/login_runtime.h`.
 *
 * PR B+C+D #11 of the dedicated plan extracts these declarations
 * unchanged byte-for-byte from `auth/login_runtime.h` so callers
 * keep the same ABI and linkage.
 */

#include <stddef.h>

int login_window_contract_evaluate(const struct login_runtime_ops *ops,
                                   struct login_window_contract *out);
int login_recovery_resume_policy_evaluate(
    const struct login_runtime_ops *ops, int recovery_session_active,
    int resume_requested, struct login_recovery_resume_policy *out);
int login_window_credential_policy_from_contract(
    const struct login_window_contract *contract,
    struct login_window_credential_policy *out);
int login_window_credential_auth_policy_from_contract(
    const struct login_window_contract *contract,
    struct login_window_credential_policy *out);
int login_window_credential_buffer_init(
    struct login_window_credential_buffer *buffer, char *storage,
    size_t storage_size, const struct login_window_credential_policy *policy);
int login_window_credential_buffer_append(
    struct login_window_credential_buffer *buffer, char ch);
int login_window_credential_buffer_backspace(
    struct login_window_credential_buffer *buffer);
int login_window_credential_buffer_masked_text(
    const struct login_window_credential_buffer *buffer, char *out,
    size_t out_size);
int login_window_credential_buffer_wipe(
    struct login_window_credential_buffer *buffer);
int login_window_credential_submit_gate_evaluate(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    struct login_window_credential_submit_gate *out);
int login_window_credential_submit_attempt_consume(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer,
    struct login_window_credential_submit_attempt *out);
int login_window_credential_auth_submit_consume(
    const struct login_window_credential_policy *policy,
    const char *username,
    struct login_window_credential_buffer *buffer,
    login_window_credential_authenticate_fn authenticate,
    struct user_record *out_user,
    struct login_window_credential_auth_submit *out);
int login_window_credential_auth_submit_userdb_consume(
    const struct login_window_credential_policy *policy,
    const char *username,
    struct login_window_credential_buffer *buffer,
    struct user_record *out_user,
    struct login_window_credential_auth_submit *out);
int login_window_credential_input_apply(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    struct login_window_credential_input_result *out);
int login_window_credential_field_view_build(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer, char *masked_out,
    size_t masked_out_size, struct login_window_credential_field_view *out);
int login_window_credential_panel_build(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    const struct login_window_credential_input_result *last_input,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_panel *out);
int login_window_credential_interaction_step(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_interaction *out);
int login_window_credential_readiness_evaluate(
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_buffer *buffer,
    const struct login_window_credential_panel *panel,
    const struct login_window_credential_interaction *interaction,
    struct login_window_credential_readiness *out);
int login_window_credential_audit_event_build(
    const struct login_window_credential_readiness *readiness,
    const struct login_window_credential_interaction *interaction,
    struct login_window_credential_audit_event *out);
int login_window_credential_view_model_build(
    const struct login_window_credential_readiness *readiness,
    const struct login_window_credential_audit_event *audit,
    struct login_window_credential_view_model *out);
int login_window_credential_ui_step_build(
    const struct login_window_credential_policy *policy,
    struct login_window_credential_buffer *buffer, int action, char ch,
    char *masked_out, size_t masked_out_size,
    struct login_window_credential_ui_step *out);
int login_window_credential_ui_session_build(
    const struct login_window_credential_policy *policy,
    char *storage, size_t storage_size, int action, char ch,
    char *masked_scratch, size_t masked_scratch_size,
    struct login_window_credential_ui_session *out);
int login_window_credential_recovery_view_model_build(
    const struct login_window_contract *contract,
    const struct login_window_credential_policy *policy,
    const struct login_window_credential_ui_session *credential_session,
    const struct login_recovery_resume_policy *resume_policy,
    struct login_window_credential_recovery_view_model *out);
int login_window_credential_screen_view_model_build(
    const struct login_window_contract *contract,
    const struct login_window_view_model *login_view,
    const struct login_window_credential_ui_session *credential_session,
    const struct login_window_credential_recovery_view_model *recovery_view,
    struct login_window_credential_screen_view_model *out);
int login_window_credential_screen_session_build(
    const struct login_runtime_ops *ops, const char *language,
    char *storage, size_t storage_size, int action, char ch,
    char *masked_scratch, size_t masked_scratch_size,
    int recovery_session_active, int resume_requested,
    struct login_window_credential_screen_session *out);
int login_window_credential_screen_render_plan_build(
    const struct login_window_credential_screen_session *session,
    struct login_window_credential_screen_render_plan *out);
int login_window_credential_screen_action_plan_build(
    const struct login_window_credential_screen_render_plan *render_plan,
    int requested_action,
    struct login_window_credential_screen_action_plan *out);
int login_window_credential_screen_ui_event_build(
    const struct login_window_credential_screen_action_plan *action_plan,
    struct login_window_credential_screen_ui_event *out);
int login_window_credential_screen_route_plan_build(
    const struct login_window_credential_screen_ui_event *ui_event,
    struct login_window_credential_screen_route_plan *out);
int login_window_credential_screen_controller_build(
    const struct login_window_credential_screen_route_plan *route_plan,
    struct login_window_credential_screen_controller *out);
int login_window_credential_recovery_decision_build(
    const struct login_window_credential_screen_controller *controller,
    const struct login_window_credential_auth_submit *auth_submit,
    struct login_window_credential_recovery_decision *out);
int login_window_credential_session_handoff_build(
    const struct login_window_credential_auth_submit *auth_submit,
    const struct login_window_credential_recovery_decision *recovery_decision,
    const struct user_record *authenticated_user,
    struct login_window_credential_session_handoff *out);
int login_window_credential_screen_presenter_build(
    const struct login_window_credential_screen_controller *controller,
    struct login_window_credential_screen_presenter *out);
int login_window_credential_screen_binding_build(
    const struct login_window_credential_screen_presenter *presenter,
    struct login_window_credential_screen_binding *out);
int login_window_credential_screen_mount_plan_build(
    const struct login_window_credential_screen_binding *binding,
    struct login_window_credential_screen_mount_plan *out);
int login_window_credential_screen_commit_plan_build(
    const struct login_window_credential_screen_mount_plan *mount_plan,
    struct login_window_credential_screen_commit_plan *out);
int login_window_credential_screen_handoff_plan_build(
    const struct login_window_credential_screen_commit_plan *commit_plan,
    struct login_window_credential_screen_handoff_plan *out);
int login_window_credential_screen_dispatch_plan_build(
    const struct login_window_credential_screen_handoff_plan *handoff_plan,
    struct login_window_credential_screen_dispatch_plan *out);
int login_window_credential_screen_queue_plan_build(
    const struct login_window_credential_screen_dispatch_plan *dispatch_plan,
    struct login_window_credential_screen_queue_plan *out);
int login_window_credential_screen_activation_plan_build(
    const struct login_window_credential_screen_queue_plan *queue_plan,
    struct login_window_credential_screen_activation_plan *out);
int login_window_credential_screen_frame_plan_build(
    const struct login_window_credential_screen_activation_plan *activation_plan,
    struct login_window_credential_screen_frame_plan *out);
int login_window_credential_screen_surface_plan_build(
    const struct login_window_credential_screen_frame_plan *frame_plan,
    struct login_window_credential_screen_surface_plan *out);
int login_window_credential_screen_compositor_plan_build(
    const struct login_window_credential_screen_surface_plan *surface_plan,
    struct login_window_credential_screen_compositor_plan *out);
int login_window_credential_screen_damage_plan_build(
    const struct login_window_credential_screen_compositor_plan *compositor_plan,
    struct login_window_credential_screen_damage_plan *out);
int login_window_credential_screen_present_plan_build(
    const struct login_window_credential_screen_damage_plan *damage_plan,
    struct login_window_credential_screen_present_plan *out);
int login_window_credential_screen_schedule_plan_build(
    const struct login_window_credential_screen_present_plan *present_plan,
    struct login_window_credential_screen_schedule_plan *out);
int login_window_credential_screen_vsync_plan_build(
    const struct login_window_credential_screen_schedule_plan *schedule_plan,
    struct login_window_credential_screen_vsync_plan *out);
int login_window_credential_screen_scanout_plan_build(
    const struct login_window_credential_screen_vsync_plan *vsync_plan,
    struct login_window_credential_screen_scanout_plan *out);
int login_window_credential_screen_display_plan_build(
    const struct login_window_credential_screen_scanout_plan *scanout_plan,
    struct login_window_credential_screen_display_plan *out);
int login_window_credential_screen_output_plan_build(
    const struct login_window_credential_screen_display_plan *display_plan,
    struct login_window_credential_screen_output_plan *out);
int login_window_credential_screen_blit_plan_build(
    const struct login_window_credential_screen_output_plan *output_plan,
    struct login_window_credential_screen_blit_plan *out);
int login_window_credential_screen_framebuffer_plan_build(
    const struct login_window_credential_screen_blit_plan *blit_plan,
    struct login_window_credential_screen_framebuffer_plan *out);
int login_window_credential_screen_flush_plan_build(
    const struct login_window_credential_screen_framebuffer_plan *framebuffer_plan,
    struct login_window_credential_screen_flush_plan *out);
int login_window_credential_screen_barrier_plan_build(
    const struct login_window_credential_screen_flush_plan *flush_plan,
    struct login_window_credential_screen_barrier_plan *out);
int login_window_credential_screen_fence_plan_build(
    const struct login_window_credential_screen_barrier_plan *barrier_plan,
    struct login_window_credential_screen_fence_plan *out);
int login_window_credential_screen_timeline_plan_build(
    const struct login_window_credential_screen_fence_plan *fence_plan,
    struct login_window_credential_screen_timeline_plan *out);
int login_window_credential_screen_sync_plan_build(
    const struct login_window_credential_screen_timeline_plan *timeline_plan,
    struct login_window_credential_screen_sync_plan *out);
int login_window_credential_screen_deadline_plan_build(
    const struct login_window_credential_screen_sync_plan *sync_plan,
    struct login_window_credential_screen_deadline_plan *out);
int login_window_credential_screen_completion_plan_build(
    const struct login_window_credential_screen_deadline_plan *deadline_plan,
    struct login_window_credential_screen_completion_plan *out);
int login_window_credential_screen_ack_plan_build(
    const struct login_window_credential_screen_completion_plan *completion_plan,
    struct login_window_credential_screen_ack_plan *out);
int login_window_credential_screen_retire_plan_build(
    const struct login_window_credential_screen_ack_plan *ack_plan,
    struct login_window_credential_screen_retire_plan *out);
int login_window_credential_screen_cleanup_plan_build(
    const struct login_window_credential_screen_retire_plan *retire_plan,
    struct login_window_credential_screen_cleanup_plan *out);
int login_window_credential_screen_seal_plan_build(
    const struct login_window_credential_screen_cleanup_plan *cleanup_plan,
    struct login_window_credential_screen_seal_plan *out);
int login_window_credential_screen_audit_plan_build(
    const struct login_window_credential_screen_seal_plan *seal_plan,
    struct login_window_credential_screen_audit_plan *out);
int login_window_credential_screen_record_plan_build(
    const struct login_window_credential_screen_audit_plan *audit_plan,
    struct login_window_credential_screen_record_plan *out);
int login_window_credential_screen_receipt_plan_build(
    const struct login_window_credential_screen_record_plan *record_plan,
    struct login_window_credential_screen_receipt_plan *out);
int login_window_credential_screen_ledger_plan_build(
    const struct login_window_credential_screen_receipt_plan *receipt_plan,
    struct login_window_credential_screen_ledger_plan *out);
int login_window_credential_screen_journal_plan_build(
    const struct login_window_credential_screen_ledger_plan *ledger_plan,
    struct login_window_credential_screen_journal_plan *out);
int login_window_credential_screen_archive_plan_build(
    const struct login_window_credential_screen_journal_plan *journal_plan,
    struct login_window_credential_screen_archive_plan *out);
int login_window_credential_screen_retention_plan_build(
    const struct login_window_credential_screen_archive_plan *archive_plan,
    struct login_window_credential_screen_retention_plan *out);
int login_window_credential_screen_expiry_plan_build(
    const struct login_window_credential_screen_retention_plan *retention_plan,
    struct login_window_credential_screen_expiry_plan *out);
int login_window_credential_screen_purge_plan_build(
    const struct login_window_credential_screen_expiry_plan *expiry_plan,
    struct login_window_credential_screen_purge_plan *out);
int login_window_credential_screen_tombstone_plan_build(
    const struct login_window_credential_screen_purge_plan *purge_plan,
    struct login_window_credential_screen_tombstone_plan *out);
int login_window_credential_screen_compaction_plan_build(
    const struct login_window_credential_screen_tombstone_plan *tombstone_plan,
    struct login_window_credential_screen_compaction_plan *out);
int login_window_credential_screen_reclaim_plan_build(
    const struct login_window_credential_screen_compaction_plan *compaction_plan,
    struct login_window_credential_screen_reclaim_plan *out);
int login_window_credential_screen_release_plan_build(
    const struct login_window_credential_screen_reclaim_plan *reclaim_plan,
    struct login_window_credential_screen_release_plan *out);
int login_window_credential_screen_gui_plan_build(
    const struct login_window_credential_screen_release_plan *release_plan,
    struct login_window_credential_screen_gui_plan *out);
int login_window_credential_screen_window_plan_build(
    const struct login_window_credential_screen_gui_plan *gui_plan,
    struct login_window_credential_screen_window_plan *out);
int login_window_credential_screen_window_surface_plan_build(
    const struct login_window_credential_screen_window_plan *window_plan,
    struct login_window_credential_screen_window_surface_plan *out);
int login_window_credential_screen_window_compositor_plan_build(
    const struct login_window_credential_screen_window_surface_plan *surface_plan,
    struct login_window_credential_screen_window_compositor_plan *out);
int login_window_credential_screen_window_damage_plan_build(
    const struct login_window_credential_screen_window_compositor_plan
        *compositor_plan,
    struct login_window_credential_screen_window_damage_plan *out);
int login_window_credential_screen_window_present_plan_build(
    const struct login_window_credential_screen_window_damage_plan *damage_plan,
    struct login_window_credential_screen_window_present_plan *out);
int login_window_credential_screen_window_schedule_plan_build(
    const struct login_window_credential_screen_window_present_plan *present_plan,
    struct login_window_credential_screen_window_schedule_plan *out);
int login_window_credential_screen_window_vsync_plan_build(
    const struct login_window_credential_screen_window_schedule_plan *schedule_plan,
    struct login_window_credential_screen_window_vsync_plan *out);
int login_window_credential_screen_window_scanout_plan_build(
    const struct login_window_credential_screen_window_vsync_plan *vsync_plan,
    struct login_window_credential_screen_window_scanout_plan *out);
int login_window_credential_screen_window_display_plan_build(
    const struct login_window_credential_screen_window_scanout_plan *scanout_plan,
    struct login_window_credential_screen_window_display_plan *out);
int login_window_credential_screen_window_output_plan_build(
    const struct login_window_credential_screen_window_display_plan *display_plan,
    struct login_window_credential_screen_window_output_plan *out);
int login_window_credential_screen_window_blit_plan_build(
    const struct login_window_credential_screen_window_output_plan *output_plan,
    struct login_window_credential_screen_window_blit_plan *out);
int login_window_credential_screen_window_commit_plan_build(
    const struct login_window_credential_screen_window_blit_plan *blit_plan,
    struct login_window_credential_screen_window_commit_plan *out);
int login_window_credential_screen_window_flip_plan_build(
    const struct login_window_credential_screen_window_commit_plan *commit_plan,
    struct login_window_credential_screen_window_flip_plan *out);
int login_window_credential_screen_window_vblank_plan_build(
    const struct login_window_credential_screen_window_flip_plan *flip_plan,
    struct login_window_credential_screen_window_vblank_plan *out);
int login_window_credential_screen_window_event_plan_build(
    const struct login_window_credential_screen_window_vblank_plan *vblank_plan,
    struct login_window_credential_screen_window_event_plan *out);
int login_window_credential_screen_window_input_plan_build(
    const struct login_window_credential_screen_window_event_plan *event_plan,
    struct login_window_credential_screen_window_input_plan *out);
int login_window_credential_screen_pipeline_safety_report_build(
    const struct login_window_credential_screen_window_input_plan *input_plan,
    struct login_window_credential_screen_pipeline_safety_report *out);
int login_window_view_model_build(const struct login_window_contract *contract,
                                  const char *language,
                                  struct login_window_view_model *out);
int login_runtime_run(struct login_runtime_ops *ops);

#endif /* AUTH_LOGIN_RUNTIME_FUNCTION_DECLARATIONS_H */
