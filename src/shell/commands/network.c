#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"

#if defined(__x86_64__)
#include "net/stack.h"
#endif

enum net_text_id {
  NET_HELP_STATUS = 0,
  NET_HELP_IP,
  NET_HELP_DNS,
  NET_HELP_GW,
  NET_HELP_SET,
  NET_HELP_HEY,
  NET_STACK_UNAVAILABLE,
  NET_RUNTIME_UNAVAILABLE,
  NET_COMMAND_UNSUPPORTED,
  NET_INVALID_USAGE,
  NET_INVALID_PARAMS,
  NET_APPLY_FAILED,
  NET_APPLIED,
  NET_REQUIRE_DESTINATION,
  NET_STACK_NOT_READY,
  NET_INVALID_TARGET,
  NET_NO_REPLY,
};

static const char *net_text(const char *language, enum net_text_id id) {
  switch (id) {
  case NET_HELP_STATUS:
    return localization_select(
        language,
        "Exibe estado da rede: driver ativo, IPv4, gateway, ARP e contadores.",
        "Shows network state: active driver, IPv4, gateway, ARP and counters.",
        "Muestra el estado de red: driver activo, IPv4, gateway, ARP y contadores.");
  case NET_HELP_IP:
    return localization_select(language,
                               "Exibe o IPv4 local e mascara de rede atuais.",
                               "Shows the current local IPv4 and network mask.",
                               "Muestra el IPv4 local y la mascara de red actual.");
  case NET_HELP_DNS:
    return localization_select(language,
                               "Exibe o servidor DNS configurado atualmente.",
                               "Shows the currently configured DNS server.",
                               "Muestra el servidor DNS configurado actualmente.");
  case NET_HELP_GW:
    return localization_select(language,
                               "Exibe o gateway padrao configurado.",
                               "Shows the configured default gateway.",
                               "Muestra el gateway predeterminado configurado.");
  case NET_HELP_SET:
    return localization_select(language,
                               "Aplica configuracao IPv4 estatica na stack atual de rede.",
                               "Applies a static IPv4 configuration to the current network stack.",
                               "Aplica una configuracion IPv4 estatica a la pila de red actual.");
  case NET_HELP_HEY:
    return localization_select(
        language,
        "Envia ICMP echo (ping) e responde com o host remoto e a latencia.",
        "Sends an ICMP echo (ping) and reports the remote host and latency.",
        "Envia ICMP echo (ping) y muestra el host remoto y la latencia.");
  case NET_STACK_UNAVAILABLE:
    return localization_select(language,
                               "stack de rede nao inicializada",
                               "network stack not initialized",
                               "pila de red no inicializada");
  case NET_RUNTIME_UNAVAILABLE:
    return localization_select(language,
                               "rede indisponivel neste runtime",
                               "network unavailable in this runtime",
                               "red no disponible en este runtime");
  case NET_COMMAND_UNSUPPORTED:
    return localization_select(language,
                               "comando nao suportado neste runtime",
                               "command not supported in this runtime",
                               "comando no soportado en este runtime");
  case NET_INVALID_USAGE:
    return localization_select(language, "uso invalido", "invalid usage",
                               "uso invalido");
  case NET_INVALID_PARAMS:
    return localization_select(language,
                               "parametros invalidos; use IPv4 no formato a.b.c.d",
                               "invalid parameters; use IPv4 in a.b.c.d format",
                               "parametros invalidos; usa IPv4 en el formato a.b.c.d");
  case NET_APPLY_FAILED:
    return localization_select(language,
                               "falha ao aplicar configuracao de rede",
                               "failed to apply network configuration",
                               "fallo al aplicar la configuracion de red");
  case NET_APPLIED:
    return localization_select(language, "configuracao de rede aplicada",
                               "network configuration applied",
                               "configuracion de red aplicada");
  case NET_REQUIRE_DESTINATION:
    return localization_select(language, "informe destino",
                               "provide destination",
                               "indica destino");
  case NET_STACK_NOT_READY:
    return localization_select(language, "stack de rede nao esta pronta",
                               "network stack is not ready",
                               "la pila de red no esta lista");
  case NET_INVALID_TARGET:
    return localization_select(language,
                               "destino invalido (use IPv4, gateway, dns ou self)",
                               "invalid destination (use IPv4, gateway, dns or self)",
                               "destino invalido (usa IPv4, gateway, dns o self)");
  case NET_NO_REPLY:
  default:
    return localization_select(language, "sem resposta do destino",
                               "no reply from destination",
                               "sin respuesta del destino");
  }
}

#if defined(__x86_64__)
static int parse_ipv4(const char *text, uint32_t *out) {
  if (!text || !out) {
    return -1;
  }
  uint32_t parts[4] = {0, 0, 0, 0};
  uint32_t idx = 0;
  uint32_t val = 0;
  int has_digit = 0;

  for (const char *p = text;; ++p) {
    char c = *p;
    if (c >= '0' && c <= '9') {
      has_digit = 1;
      val = (val * 10u) + (uint32_t)(c - '0');
      if (val > 255u) {
        return -1;
      }
      continue;
    }
    if (c == '.' || c == '\0') {
      if (!has_digit || idx >= 4u) {
        return -1;
      }
      parts[idx++] = val;
      val = 0;
      has_digit = 0;
      if (c == '\0') {
        break;
      }
      continue;
    }
    return -1;
  }

  if (idx != 4u) {
    return -1;
  }

  *out = ((parts[0] & 0xFFu) << 24) | ((parts[1] & 0xFFu) << 16) |
         ((parts[2] & 0xFFu) << 8) | (parts[3] & 0xFFu);
  return 0;
}

static int read_net_status(const char *language, struct net_stack_status *out) {
  if (!out) {
    return -1;
  }
  if (net_stack_status(out) != 0) {
    shell_print_error(net_text(language, NET_STACK_UNAVAILABLE));
    return -1;
  }
  return 0;
}

static uint32_t resolve_target_ip(const char *target,
                                  const struct net_stack_status *st) {
  if (!target || !st) {
    return 0;
  }
  if (shell_string_equal(target, "gateway") || shell_string_equal(target, "gw")) {
    return st->ipv4.gateway;
  }
  if (shell_string_equal(target, "dns")) {
    return st->ipv4.dns;
  }
  if (shell_string_equal(target, "self") || shell_string_equal(target, "ip")) {
    return st->ipv4.addr;
  }
  return 0;
}
#endif

static int cmd_net_status(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-status",
                        net_text(language, NET_HELP_STATUS))) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_RUNTIME_UNAVAILABLE));
  return -1;
#else
  struct net_stack_status st;
  if (read_net_status(language, &st) != 0) {
    return -1;
  }

  char ip[16], mask[16], gw[16], dns[16];
  net_ipv4_format(st.ipv4.addr, ip);
  net_ipv4_format(st.ipv4.mask, mask);
  net_ipv4_format(st.ipv4.gateway, gw);
  net_ipv4_format(st.ipv4.dns, dns);

  shell_print("driver=");
  shell_print(net_driver_name(st.nic.kind));
  shell_print(" ready=");
  shell_print(st.ready ? "yes\n" : "no\n");

  shell_print("ipv4=");
  shell_print(ip);
  shell_print(" mask=");
  shell_print(mask);
  shell_print(" gw=");
  shell_print(gw);
  shell_print(" dns=");
  shell_print(dns);
  shell_newline();

  shell_print("arp_entries=");
  shell_print_number(st.arp_entries);
  shell_print(" tx=");
  shell_print_number((uint32_t)st.stats.frames_tx);
  shell_print(" rx=");
  shell_print_number((uint32_t)st.stats.frames_rx);
  shell_print(" drop=");
  shell_print_number((uint32_t)st.stats.frames_drop);
  shell_newline();
  return 0;
#endif
}

static int cmd_net_ip(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-ip", net_text(language, NET_HELP_IP))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (read_net_status(language, &st) != 0) {
    return -1;
  }

  char ip[16], mask[16];
  net_ipv4_format(st.ipv4.addr, ip);
  net_ipv4_format(st.ipv4.mask, mask);
  shell_print("ipv4=");
  shell_print(ip);
  shell_print(" mask=");
  shell_print(mask);
  shell_newline();
  return 0;
#endif
}

static int cmd_net_dns(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-dns", net_text(language, NET_HELP_DNS))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (read_net_status(language, &st) != 0) {
    return -1;
  }
  char dns[16];
  net_ipv4_format(st.ipv4.dns, dns);
  shell_print("dns=");
  shell_print(dns);
  shell_newline();
  return 0;
#endif
}

static int cmd_net_gw(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-gw", net_text(language, NET_HELP_GW))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (read_net_status(language, &st) != 0) {
    return -1;
  }
  char gw[16];
  net_ipv4_format(st.ipv4.gateway, gw);
  shell_print("gateway=");
  shell_print(gw);
  shell_newline();
  return 0;
#endif
}

static int cmd_net_set(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-set <ip> <mask> <gateway> <dns>",
                        net_text(language, NET_HELP_SET))) {
    return 0;
  }

  if (argc != 5) {
    shell_print_error(net_text(language, NET_INVALID_USAGE));
    shell_suggest_help("net-set");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  uint32_t ip = 0;
  uint32_t mask = 0;
  uint32_t gw = 0;
  uint32_t dns = 0;
  if (parse_ipv4(argv[1], &ip) != 0 || parse_ipv4(argv[2], &mask) != 0 ||
      parse_ipv4(argv[3], &gw) != 0 || parse_ipv4(argv[4], &dns) != 0) {
    shell_print_error(net_text(language, NET_INVALID_PARAMS));
    return -1;
  }

  if (net_stack_set_ipv4(ip, mask, gw, dns) != 0) {
    shell_print_error(net_text(language, NET_APPLY_FAILED));
    return -1;
  }

  shell_print_ok(net_text(language, NET_APPLIED));
  return cmd_net_status(ctx, 1, argv);
#endif
}

static int cmd_hey(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "hey <ip|gateway|dns|self>",
                        net_text(language, NET_HELP_HEY))) {
    return 0;
  }
  if (argc < 2) {
    shell_print_error(net_text(language, NET_REQUIRE_DESTINATION));
    shell_suggest_help("hey");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (!net_stack_ready()) {
    shell_print_error(net_text(language, NET_STACK_NOT_READY));
    return -1;
  }

  const char *target = argv[1];
  struct net_stack_status st;
  if (read_net_status(language, &st) != 0) {
    return -1;
  }
  uint32_t dst_ip = 0;
  uint32_t alias_ip = resolve_target_ip(target, &st);
  if (alias_ip != 0u) {
    dst_ip = alias_ip;
  } else if (parse_ipv4(target, &dst_ip) != 0) {
    shell_print_error(net_text(language, NET_INVALID_TARGET));
    return -1;
  }

  uint32_t rtt_ms = 0;
  uint32_t reply_ip = 0;
  if (net_stack_ping(dst_ip, 1200u, &rtt_ms, &reply_ip) != 0) {
    shell_print_error(net_text(language, NET_NO_REPLY));
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
  shell_print(dst_text);
  shell_print("/");
  shell_print(reply_text);
  shell_print(") ");
  shell_print_number(rtt_ms);
  shell_print("ms)\n");
  return 0;
#endif
}

static struct shell_command g_network_commands[6];
static int g_network_commands_initialized = 0;

static void init_network_commands(void) {
  if (g_network_commands_initialized) {
    return;
  }
  g_network_commands[0].name = "hey";
  g_network_commands[0].handler = cmd_hey;
  g_network_commands[1].name = "net-status";
  g_network_commands[1].handler = cmd_net_status;
  g_network_commands[2].name = "net-ip";
  g_network_commands[2].handler = cmd_net_ip;
  g_network_commands[3].name = "net-dns";
  g_network_commands[3].handler = cmd_net_dns;
  g_network_commands[4].name = "net-gw";
  g_network_commands[4].handler = cmd_net_gw;
  g_network_commands[5].name = "net-set";
  g_network_commands[5].handler = cmd_net_set;
  g_network_commands_initialized = 1;
}

const struct shell_command *shell_commands_network(size_t *count) {
  init_network_commands();
  if (count) {
    *count = 6;
  }
  return g_network_commands;
}
