#ifndef SHELL_COMMANDS_NETWORK_INTERNAL_H
#define SHELL_COMMANDS_NETWORK_INTERNAL_H

#include "shell/commands.h"
#include "shell/core.h"

#include "lang/localization.h"
#include "core/system_init.h"

#if defined(__x86_64__)
#include "arch/x86_64/storage_runtime.h"
#include "net/stack.h"
#endif

#include <stdint.h>

enum net_text_id {
  NET_HELP_STATUS = 0,
  NET_HELP_IP,
  NET_HELP_DNS,
  NET_HELP_GW,
  NET_HELP_DUMP,
  NET_HELP_SET,
  NET_HELP_MODE,
  NET_HELP_REFRESH,
  NET_HELP_RESOLVE,
  NET_HELP_HEY,
  NET_HELP_FETCH,
  NET_STACK_UNAVAILABLE,
  NET_RUNTIME_UNAVAILABLE,
  NET_COMMAND_UNSUPPORTED,
  NET_INVALID_USAGE,
  NET_INVALID_PARAMS,
  NET_APPLY_FAILED,
  NET_APPLIED,
  NET_REFRESHED,
  NET_REFRESH_NOOP,
  NET_REFRESH_WAIT_PLATFORM,
  NET_REFRESH_WAIT_STORAGE,
  NET_REFRESH_WAIT_BUS,
  NET_REFRESH_WAIT_OFFER,
  NET_REFRESH_FAILED,
  NET_REQUIRE_DESTINATION,
  NET_STACK_NOT_READY,
  NET_INVALID_TARGET,
  NET_DNS_LOOKUP_FAILED,
  NET_NO_REPLY,
};

const char *net_cli_text(const char *language, enum net_text_id id);
const char *net_cli_current_mode(const struct shell_context *ctx);
void net_cli_print_mode_list(const char *language);

#if defined(__x86_64__)
int net_cli_parse_ipv4(const char *text, uint32_t *out);
int net_cli_read_status(const char *language, struct net_stack_status *out);
uint32_t net_cli_resolve_target_ip(const char *target,
                                   const struct net_stack_status *st);
const char *net_cli_runtime_label(const struct net_stack_status *st);
const char *net_cli_hyperv_stage_label(const struct net_stack_status *st);
const char *net_cli_hyperv_bus_label(const struct net_stack_status *st);
const char *net_cli_hyperv_runtime_phase_label(
    const struct net_stack_status *st);
const char *net_cli_hyperv_block_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform);
const char *net_cli_hyperv_offer_cache_label(
    const struct net_stack_status *st);
const char *net_cli_hyperv_refresh_action_label(
    const struct net_stack_status *st);
const char *net_cli_hyperv_effective_next_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform);
const char *net_cli_hyperv_gate_label(const struct net_stack_status *st);
int net_cli_hyperv_platform_blocked(struct system_runtime_platform *platform);
void net_cli_print_hex16(uint16_t value);
void net_cli_print_runtime_block_detail(const struct net_stack_status *st);
#endif

int net_cmd_status(struct shell_context *ctx, int argc, char **argv);
int net_cmd_mode(struct shell_context *ctx, int argc, char **argv);
int net_cmd_ip(struct shell_context *ctx, int argc, char **argv);
int net_cmd_refresh(struct shell_context *ctx, int argc, char **argv);
int net_cmd_dump_runtime(struct shell_context *ctx, int argc, char **argv);
int net_cmd_resolve(struct shell_context *ctx, int argc, char **argv);
int net_cmd_dns(struct shell_context *ctx, int argc, char **argv);
int net_cmd_gw(struct shell_context *ctx, int argc, char **argv);
int net_cmd_set(struct shell_context *ctx, int argc, char **argv);
int net_cmd_ping(struct shell_context *ctx, int argc, char **argv);
int net_cmd_fetch(struct shell_context *ctx, int argc, char **argv);

#endif
