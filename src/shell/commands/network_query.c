#include "network_internal.h"

#if defined(__x86_64__)
static int net_query_lookup_target_ip(const char *language, const char *target,
                                      const struct net_stack_status *st,
                                      uint32_t *out_ip) {
  uint32_t alias_ip = 0;

  if (!target || !st || !out_ip) {
    return -1;
  }

  alias_ip = net_cli_resolve_target_ip(target, st);
  if (alias_ip != 0u) {
    *out_ip = alias_ip;
    return 0;
  }
  if (net_cli_parse_ipv4(target, out_ip) == 0) {
    return 0;
  }
  if (net_stack_dns_resolve(target, 2500u, out_ip) == 0) {
    return 1;
  }
  (void)language;
  return -2;
}
#endif

int net_cmd_resolve(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-resolve <hostname>",
                        net_cli_text(language, NET_HELP_RESOLVE))) {
    return 0;
  }

  if (argc != 2) {
    shell_print_error(net_cli_text(language, NET_INVALID_USAGE));
    shell_suggest_help("net-resolve");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (!net_stack_ready()) {
    struct net_stack_status st;
    shell_print_error(net_cli_text(language, NET_STACK_NOT_READY));
    if (net_cli_read_status(language, &st) == 0) {
      net_cli_print_runtime_block_detail(&st);
    }
    return -1;
  }

  uint32_t resolved_ip = 0;
  if (net_stack_dns_resolve(argv[1], 2500u, &resolved_ip) != 0) {
    shell_print_error(net_cli_text(language, NET_DNS_LOOKUP_FAILED));
    return -1;
  }

  char ip[16];
  net_ipv4_format(resolved_ip, ip);
  shell_print("name=");
  shell_print(argv[1]);
  shell_print(" ipv4=");
  shell_print(ip);
  shell_newline();
  return 0;
#endif
}

int net_cmd_ping(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "hey <ip|hostname|gateway|dns|self>",
                        net_cli_text(language, NET_HELP_HEY))) {
    return 0;
  }
  if (argc < 2) {
    shell_print_error(net_cli_text(language, NET_REQUIRE_DESTINATION));
    shell_suggest_help("hey");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (!net_stack_ready()) {
    struct net_stack_status st;
    shell_print_error(net_cli_text(language, NET_STACK_NOT_READY));
    if (net_cli_read_status(language, &st) == 0) {
      net_cli_print_runtime_block_detail(&st);
    }
    return -1;
  }

  const char *target = argv[1];
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }
  uint32_t dst_ip = 0;
  int resolved_via_dns =
      net_query_lookup_target_ip(language, target, &st, &dst_ip);
  if (resolved_via_dns < 0) {
    shell_print_error(net_cli_text(language, NET_DNS_LOOKUP_FAILED));
    return -1;
  }

  uint32_t rtt_ms = 0;
  uint32_t reply_ip = 0;
  if (net_stack_ping(dst_ip, 1200u, &rtt_ms, &reply_ip) != 0) {
    shell_print_error(net_cli_text(language, NET_NO_REPLY));
    return -1;
  }
  if (rtt_ms == 0) {
    rtt_ms = 1;
  }

  char dst_text[16];
  char reply_text[16];
  net_ipv4_format(dst_ip, dst_text);
  net_ipv4_format(reply_ip ? reply_ip : dst_ip, reply_text);

  shell_print("hello from (");
  if (resolved_via_dns > 0) {
    shell_print(target);
    shell_print("/");
  }
  shell_print(dst_text);
  shell_print("/");
  shell_print(reply_text);
  shell_print(") ");
  shell_print_number(rtt_ms);
  shell_print("ms)\n");
  return 0;
#endif
}
