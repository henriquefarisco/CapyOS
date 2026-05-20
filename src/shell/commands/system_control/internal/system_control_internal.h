#ifndef SYSTEM_CONTROL_INTERNAL_H
#define SYSTEM_CONTROL_INTERNAL_H

#include "shell/commands.h"
#include "shell/core.h"
#include "lang/localization.h"
#include "kernel/log/klog_persist.h"
#include "services/service_manager.h"
#include "core/system_init.h"
#include "services/update_agent.h"
#include "services/capypkg.h"
#include "services/capypkg_bootstrap.h"
#include "services/install_profile.h"
#include "auth/user.h"
#include "auth/user_home.h"
#include "auth/user_prefs.h"
#include "auth/session.h"
#include "core/version.h"
#include "core/work_queue.h"
#include "drivers/acpi/acpi.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/capyfs.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#if defined(__x86_64__)
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/storage_runtime.h"
#include "drivers/timer/pit.h"
#include "net/stack.h"
#include "drivers/hyperv/hyperv.h"
#endif

/* service_helpers */
void sync_and_flush(void);
void update_history_append_event(const char *event_name,
                                 const struct system_update_status *status);
int shell_recovery_capyfs_check(struct capyfs_check_report *out);
#if defined(__x86_64__)
int recovery_storage_ensure_base_layout(void);
int recovery_storage_rewrite_config(const struct shell_context *ctx);
int recovery_storage_reset_admin(const char *password);
#endif
int cmd_service_control(struct shell_context *ctx, int argc, char **argv);

/* config_commands */
int cmd_config_keyboard(struct shell_context *ctx, int argc, char **argv);
int cmd_config_theme(struct shell_context *ctx, int argc, char **argv);
int cmd_config_splash(struct shell_context *ctx, int argc, char **argv);
int cmd_config_language(struct shell_context *ctx, int argc, char **argv);

/* jobs_updates shared helpers (definitions in jobs_updates.c) */
int update_runtime_writer(const char *path, const char *text);
int refresh_update_agent_service_state(int rc,
                                       struct system_update_status *status);
const char *update_channel_name_or_null(const char *value);

/* jobs_updates */
int cmd_job_run(struct shell_context *ctx, int argc, char **argv);
int cmd_update_check(struct shell_context *ctx, int argc, char **argv);
int cmd_update_fetch(struct shell_context *ctx, int argc, char **argv);
int cmd_update_download_payload(struct shell_context *ctx, int argc, char **argv);
int cmd_update_prepare(struct shell_context *ctx, int argc, char **argv);
int cmd_update_prepare_dry_run(struct shell_context *ctx, int argc, char **argv);
int cmd_update_prepare_explain(struct shell_context *ctx, int argc, char **argv);
int cmd_update_stage(struct shell_context *ctx, int argc, char **argv);
int cmd_update_arm(struct shell_context *ctx, int argc, char **argv);
int cmd_update_apply(struct shell_context *ctx, int argc, char **argv);
int cmd_update_confirm_health(struct shell_context *ctx, int argc, char **argv);
int cmd_update_rollback_check(struct shell_context *ctx, int argc, char **argv);
int cmd_update_clear(struct shell_context *ctx, int argc, char **argv);
int cmd_update_import_manifest(struct shell_context *ctx, int argc, char **argv);
int cmd_update_channel(struct shell_context *ctx, int argc, char **argv);

/* service_target_resume */
int cmd_service_target(struct shell_context *ctx, int argc, char **argv);
int shell_resolve_recovery_target(struct shell_context *ctx,
                                  const char *requested_name,
                                  struct system_service_target_status *out,
                                  const char **resolved_name);
int cmd_recovery_resume(struct shell_context *ctx, int argc, char **argv);

/* recovery_login_verify */
int cmd_recovery_login(struct shell_context *ctx, int argc, char **argv);
int cmd_recovery_verify(struct shell_context *ctx, int argc, char **argv);

/* recovery_storage */
int cmd_recovery_storage_repair(struct shell_context *ctx, int argc, char **argv);
/* capypkg_commands */
int cmd_pkg_list(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_info(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_fetch(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_install(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_remove(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_update(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_source_list(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_source_add(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_source_remove(struct shell_context *ctx, int argc, char **argv);
int cmd_pkg_bootstrap(struct shell_context *ctx, int argc, char **argv);
/* The programmatic bootstrap entry point lives in
 * `include/services/capypkg_bootstrap.h` and is included above. */

/* capy_command (alpha.241 unified entry point). */
int cmd_capy(struct shell_context *ctx, int argc, char **argv);


#endif
