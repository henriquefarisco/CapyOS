#include "network_internal.h"

int net_cmd_mode(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  if (shell_handle_help(argc, argv, "net-mode [list|show|static|dhcp]",
                        net_cli_text(language, NET_HELP_MODE))) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (argc < 2 || shell_string_equal(argv[1], "list")) {
    net_cli_print_mode_list(language);
    return 0;
  }

  if (shell_string_equal(argv[1], "show")) {
    shell_print(localization_select(language, "Current network mode: ",
                                    "Current network mode: ",
                                    "Modo de red actual: "));
    shell_print(net_cli_current_mode(ctx));
    shell_newline();
    return 0;
  }

  if (shell_string_equal(argv[1], "static")) {
    struct system_settings *mutable_settings = NULL;
    if (ctx && ctx->settings) {
      mutable_settings = (struct system_settings *)ctx->settings;
      shell_copy(mutable_settings->network_mode,
                 sizeof(mutable_settings->network_mode), "static");
      if (net_stack_set_ipv4(mutable_settings->ipv4_addr,
                             mutable_settings->ipv4_mask,
                             mutable_settings->ipv4_gateway,
                             mutable_settings->ipv4_dns) != 0) {
        shell_print_error(net_cli_text(language, NET_APPLY_FAILED));
        return -1;
      }
    }
    if (system_save_network_mode("static") != 0) {
      shell_print_ok(localization_select(language,
                                         "modo de rede alterado para static\n",
                                         "network mode changed to static\n",
                                         "modo de red cambiado a static\n"));
      shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
      return 0;
    }
    shell_print_ok(localization_select(language,
                                       "modo de rede alterado para static\n",
                                       "network mode changed to static\n",
                                       "modo de red cambiado a static\n"));
    return 0;
  }

  if (shell_string_equal(argv[1], "dhcp")) {
    struct system_settings *mutable_settings = NULL;
    if (net_stack_dhcp_acquire(2500u) != 0) {
      shell_print_error(localization_select(language,
                                            "falha ao obter lease DHCP",
                                            "failed to acquire DHCP lease",
                                            "fallo al obtener lease DHCP"));
      return -1;
    }
    if (ctx && ctx->settings) {
      mutable_settings = (struct system_settings *)ctx->settings;
      shell_copy(mutable_settings->network_mode,
                 sizeof(mutable_settings->network_mode), "dhcp");
    }
    if (system_save_network_mode("dhcp") != 0) {
      shell_print_ok(localization_select(language,
                                         "modo de rede alterado para dhcp\n",
                                         "network mode changed to dhcp\n",
                                         "modo de red cambiado a dhcp\n"));
      shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
      return net_cmd_status(ctx, 1, argv);
    }
    shell_print_ok(localization_select(language,
                                       "modo de rede alterado para dhcp\n",
                                       "network mode changed to dhcp\n",
                                       "modo de red cambiado a dhcp\n"));
    return net_cmd_status(ctx, 1, argv);
  }

  shell_print_error(localization_select(language, "modo invalido",
                                        "invalid mode", "modo invalido"));
  shell_suggest_help("net-mode");
  return -1;
#endif
}

int net_cmd_set(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-set <ip> <mask> <gateway> <dns>",
                        net_cli_text(language, NET_HELP_SET))) {
    return 0;
  }

  if (argc != 5) {
    shell_print_error(net_cli_text(language, NET_INVALID_USAGE));
    shell_suggest_help("net-set");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  uint32_t ip = 0;
  uint32_t mask = 0;
  uint32_t gw = 0;
  uint32_t dns = 0;
  if (net_cli_parse_ipv4(argv[1], &ip) != 0 ||
      net_cli_parse_ipv4(argv[2], &mask) != 0 ||
      net_cli_parse_ipv4(argv[3], &gw) != 0 ||
      net_cli_parse_ipv4(argv[4], &dns) != 0) {
    shell_print_error(net_cli_text(language, NET_INVALID_PARAMS));
    return -1;
  }

  if (net_stack_set_ipv4(ip, mask, gw, dns) != 0) {
    shell_print_error(net_cli_text(language, NET_APPLY_FAILED));
    return -1;
  }
  if (ctx && ctx->settings) {
    struct system_settings *mutable_settings =
        (struct system_settings *)ctx->settings;
    shell_copy(mutable_settings->network_mode,
               sizeof(mutable_settings->network_mode), "static");
    mutable_settings->ipv4_addr = ip;
    mutable_settings->ipv4_mask = mask;
    mutable_settings->ipv4_gateway = gw;
    mutable_settings->ipv4_dns = dns;
  }
  if (system_save_network_ipv4(ip, mask, gw, dns) != 0) {
    shell_print_ok(net_cli_text(language, NET_APPLIED));
    shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
    return net_cmd_status(ctx, 1, argv);
  }

  shell_print_ok(net_cli_text(language, NET_APPLIED));
  return net_cmd_status(ctx, 1, argv);
#endif
}
