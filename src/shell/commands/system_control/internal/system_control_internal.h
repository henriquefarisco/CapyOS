#ifndef SYSTEM_CONTROL_INTERNAL_H
#define SYSTEM_CONTROL_INTERNAL_H

#include "shell/commands.h"
#include "shell/core.h"
#include "lang/localization.h"
#include "kernel/log/klog_persist.h"
#include "services/service_manager.h"
#include "core/system_init.h"
#include "services/update_agent.h"
#include "auth/user.h"
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

/* jobs_updates */
int cmd_job_run(struct shell_context *ctx, int argc, char **argv);
int cmd_update_check(struct shell_context *ctx, int argc, char **argv);
int cmd_update_stage(struct shell_context *ctx, int argc, char **argv);
int cmd_update_arm(struct shell_context *ctx, int argc, char **argv);
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

#endif
