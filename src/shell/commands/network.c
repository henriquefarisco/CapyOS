#include "network_internal.h"

static struct shell_command g_network_commands[11];
static int g_network_commands_initialized = 0;

static int net_cmd_ping_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_ping(ctx, argc, argv);
}

static int net_cmd_status_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_status(ctx, argc, argv);
}

static int net_cmd_ip_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_ip(ctx, argc, argv);
}

static int net_cmd_dns_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_dns(ctx, argc, argv);
}

static int net_cmd_gw_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_gw(ctx, argc, argv);
}

static int net_cmd_refresh_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_refresh(ctx, argc, argv);
}

static int net_cmd_dump_runtime_entry(struct shell_context *ctx, int argc,
                                      char **argv) {
  return net_cmd_dump_runtime(ctx, argc, argv);
}

static int net_cmd_resolve_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_resolve(ctx, argc, argv);
}

static int net_cmd_set_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_set(ctx, argc, argv);
}

static int net_cmd_mode_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_mode(ctx, argc, argv);
}

static int net_cmd_fetch_entry(struct shell_context *ctx, int argc, char **argv) {
  return net_cmd_fetch(ctx, argc, argv);
}

static void init_network_commands(void) {
  if (g_network_commands_initialized) {
    return;
  }
  g_network_commands[0].name = "hey";
  g_network_commands[0].handler = net_cmd_ping_entry;
  g_network_commands[1].name = "net-status";
  g_network_commands[1].handler = net_cmd_status_entry;
  g_network_commands[2].name = "net-ip";
  g_network_commands[2].handler = net_cmd_ip_entry;
  g_network_commands[3].name = "net-dns";
  g_network_commands[3].handler = net_cmd_dns_entry;
  g_network_commands[4].name = "net-gw";
  g_network_commands[4].handler = net_cmd_gw_entry;
  g_network_commands[5].name = "net-refresh";
  g_network_commands[5].handler = net_cmd_refresh_entry;
  g_network_commands[6].name = "net-dump-runtime";
  g_network_commands[6].handler = net_cmd_dump_runtime_entry;
  g_network_commands[7].name = "net-resolve";
  g_network_commands[7].handler = net_cmd_resolve_entry;
  g_network_commands[8].name = "net-set";
  g_network_commands[8].handler = net_cmd_set_entry;
  g_network_commands[9].name = "net-mode";
  g_network_commands[9].handler = net_cmd_mode_entry;
  g_network_commands[10].name = "net-fetch";
  g_network_commands[10].handler = net_cmd_fetch_entry;
  g_network_commands_initialized = 1;
}

const struct shell_command *shell_commands_network(size_t *count) {
  init_network_commands();
  if (count) {
    *count = 11;
  }
  return g_network_commands;
}
