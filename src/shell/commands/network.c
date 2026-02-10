#include "shell/commands.h"
#include "shell/core.h"

#if defined(__x86_64__)
#include "net/stack.h"
#endif

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
#endif

static int cmd_net_status(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  if (shell_handle_help(
          argc, argv, "net-status",
          "Exibe estado da rede: driver ativo, IPv4, gateway, ARP e contadores.")) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error("rede indisponivel neste runtime");
  return -1;
#else
  struct net_stack_status st;
  if (net_stack_status(&st) != 0) {
    shell_print_error("stack de rede nao inicializada");
    return -1;
  }

  char ip[16], gw[16], dns[16];
  net_ipv4_format(st.ipv4.addr, ip);
  net_ipv4_format(st.ipv4.gateway, gw);
  net_ipv4_format(st.ipv4.dns, dns);

  shell_print("driver=");
  shell_print(net_driver_name(st.nic.kind));
  shell_print(" ready=");
  shell_print(st.ready ? "yes\n" : "no\n");

  shell_print("ipv4=");
  shell_print(ip);
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

static int cmd_hey(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  if (shell_handle_help(
          argc, argv, "hey <ip|gateway|dns>",
          "Envia ICMP echo (ping) e responde: hello from (<destino>/<host>) Xms).")) {
    return 0;
  }
  if (argc < 2) {
    shell_print_error("informe destino");
    shell_suggest_help("hey");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error("comando nao suportado neste runtime");
  return -1;
#else
  if (!net_stack_ready()) {
    shell_print_error("stack de rede nao esta pronta");
    return -1;
  }

  const char *target = argv[1];
  uint32_t dst_ip = 0;
  if (shell_string_equal(target, "gateway") || shell_string_equal(target, "gw") ||
      shell_string_equal(target, "dns")) {
    struct net_stack_status st;
    if (net_stack_status(&st) != 0) {
      shell_print_error("nao foi possivel obter configuracao de rede");
      return -1;
    }
    dst_ip = shell_string_equal(target, "dns") ? st.ipv4.dns : st.ipv4.gateway;
  } else if (parse_ipv4(target, &dst_ip) != 0) {
    shell_print_error("destino invalido (use IPv4, gateway ou dns)");
    return -1;
  }

  uint32_t rtt_ms = 0;
  uint32_t reply_ip = 0;
  if (net_stack_ping(dst_ip, 1200u, &rtt_ms, &reply_ip) != 0) {
    shell_print_error("sem resposta do destino");
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

static struct shell_command g_network_commands[2];
static int g_network_commands_initialized = 0;

static void init_network_commands(void) {
  if (g_network_commands_initialized) {
    return;
  }
  g_network_commands[0].name = "hey";
  g_network_commands[0].handler = cmd_hey;
  g_network_commands[1].name = "net-status";
  g_network_commands[1].handler = cmd_net_status;
  g_network_commands_initialized = 1;
}

const struct shell_command *shell_commands_network(size_t *count) {
  init_network_commands();
  if (count) {
    *count = 2;
  }
  return g_network_commands;
}
