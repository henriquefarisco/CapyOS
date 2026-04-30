#ifndef SYSTEM_INFO_INTERNAL_H
#define SYSTEM_INFO_INTERNAL_H

#include "shell/commands.h"
#include "shell/core.h"

#include "auth/user.h"
#include "boot/boot_metrics.h"
#include "core/version.h"
#include "core/work_queue.h"
#include "drivers/timer/pit.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/vfs.h"
#include "lang/localization.h"
#include "memory/kmem.h"
#include "net/dns_cache.h"
#include "services/service_boot_policy.h"
#include "services/service_manager.h"
#include "services/update_agent.h"

#if defined(__x86_64__)
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/storage_runtime.h"
#include "net/stack.h"
#endif

void shell_print_signed_result(int32_t value);
void shell_print_bool_flag(const char *label, int value);
void shell_print_path_status(const char *path);
int system_info_recovery_capyfs_check(struct capyfs_check_report *out);

int cmd_print_me(struct shell_context *ctx, int argc, char **argv);
int cmd_print_id(struct shell_context *ctx, int argc, char **argv);
int cmd_print_host(struct shell_context *ctx, int argc, char **argv);
int cmd_print_version(struct shell_context *ctx, int argc, char **argv);
int cmd_print_time(struct shell_context *ctx, int argc, char **argv);
int cmd_print_insomnia(struct shell_context *ctx, int argc, char **argv);
int cmd_print_envs(struct shell_context *ctx, int argc, char **argv);

int cmd_service_status(struct shell_context *ctx, int argc, char **argv);
int cmd_job_status(struct shell_context *ctx, int argc, char **argv);

int cmd_update_status(struct shell_context *ctx, int argc, char **argv);
int cmd_update_history(struct shell_context *ctx, int argc, char **argv);

int cmd_recovery_status(struct shell_context *ctx, int argc, char **argv);
int cmd_recovery_report(struct shell_context *ctx, int argc, char **argv);
int cmd_recovery_history(struct shell_context *ctx, int argc, char **argv);

int cmd_recovery_storage(struct shell_context *ctx, int argc, char **argv);
int cmd_recovery_storage_check(struct shell_context *ctx, int argc, char **argv);
int cmd_recovery_network(struct shell_context *ctx, int argc, char **argv);

int cmd_perf_boot(struct shell_context *ctx, int argc, char **argv);
int cmd_perf_net(struct shell_context *ctx, int argc, char **argv);
int cmd_perf_fs(struct shell_context *ctx, int argc, char **argv);
int cmd_perf_mem(struct shell_context *ctx, int argc, char **argv);
int cmd_perf_task(struct shell_context *ctx, int argc, char **argv);

#endif
