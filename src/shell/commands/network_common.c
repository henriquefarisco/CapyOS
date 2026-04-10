#include "network_internal.h"

#include "net/hyperv_platform_diag.h"

const char *net_cli_text(const char *language, enum net_text_id id) {
  switch (id) {
  case NET_HELP_STATUS:
    return localization_select(
        language,
        "Exibe estado da rede: modo, driver detectado, readiness do runtime, IPv4, gateway, ARP e contadores.",
        "Shows network state: mode, detected driver, runtime readiness, IPv4, gateway, ARP and counters.",
        "Muestra el estado de red: modo, controlador detectado, disponibilidad del runtime, IPv4, gateway, ARP y contadores.");
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
  case NET_HELP_DUMP:
    return localization_select(
        language,
        "Exibe um dump detalhado do runtime de rede, com gate, fase, contadores de refresh e estado Hyper-V/StorVSC.",
        "Shows a detailed dump of the network runtime, including gate, phase, refresh counters and Hyper-V/StorVSC state.",
        "Muestra un volcado detallado del runtime de red, con gate, fase, contadores de refresh y estado Hyper-V/StorVSC.");
  case NET_HELP_SET:
    return localization_select(language,
                               "Aplica configuracao IPv4 estatica na stack atual e salva em /system/config.ini.",
                               "Applies a static IPv4 configuration to the current stack and saves it in /system/config.ini.",
                               "Aplica una configuracion IPv4 estatica a la pila actual y la guarda en /system/config.ini.");
  case NET_HELP_MODE:
    return localization_select(
        language,
        "Mostra ou altera o modo de rede (`static` ou `dhcp`) e persiste em /system/config.ini.",
        "Shows or changes the network mode (`static` or `dhcp`) and persists it in /system/config.ini.",
        "Muestra o cambia el modo de red (`static` o `dhcp`) y lo guarda en /system/config.ini.");
  case NET_HELP_REFRESH:
    return localization_select(
        language,
        "Atualiza o runtime de rede; no Hyper-V, avanca o controlador NetVSC em passos pequenos e controlados.",
        "Refreshes the network runtime; on Hyper-V, it advances the NetVSC controller in small controlled steps.",
        "Actualiza el runtime de red; en Hyper-V, avanza el controlador NetVSC en pasos pequenos y controlados.");
  case NET_HELP_RESOLVE:
    return localization_select(
        language,
        "Resolve um hostname via DNS usando o servidor configurado atualmente.",
        "Resolves a hostname through DNS using the currently configured server.",
        "Resuelve un hostname por DNS usando el servidor configurado actualmente.");
  case NET_HELP_HEY:
    return localization_select(
        language,
        "Envia ICMP echo (ping) para IPv4, alias (`gateway`, `dns`, `self`) ou hostname e responde com o host remoto e a latencia.",
        "Sends an ICMP echo (ping) to an IPv4 address, alias (`gateway`, `dns`, `self`) or hostname and reports the remote host and latency.",
        "Envia ICMP echo (ping) a una IPv4, alias (`gateway`, `dns`, `self`) o hostname y muestra el host remoto y la latencia.");
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
  case NET_REFRESHED:
    return localization_select(language,
                               "runtime de rede atualizado",
                               "network runtime refreshed",
                               "runtime de red actualizado");
  case NET_REFRESH_NOOP:
    return localization_select(language,
                               "nenhuma atualizacao de runtime disponivel para esta NIC",
                               "no runtime refresh action available for this NIC",
                               "no hay accion de actualizacion disponible para esta NIC");
  case NET_REFRESH_WAIT_PLATFORM:
    return localization_select(
        language,
        "controlador Hyper-V aguardando runtime nativo da plataforma (gate de ExitBootServices ainda bloqueado)",
        "Hyper-V controller is waiting for the native platform runtime (ExitBootServices gate is still blocked)",
        "el controlador Hyper-V esta esperando el runtime nativo de la plataforma (el gate de ExitBootServices sigue bloqueado)");
  case NET_REFRESH_WAIT_STORAGE:
    return localization_select(
        language,
        "controlador Hyper-V aguardando storage nativo antes de promover a rede sintetica",
        "Hyper-V controller is waiting for native storage before promoting the synthetic network",
        "el controlador Hyper-V esta esperando almacenamiento nativo antes de promover la red sintetica");
  case NET_REFRESH_WAIT_BUS:
    return localization_select(
        language,
        "controlador Hyper-V aguardando conexao segura do barramento VMBus",
        "Hyper-V controller is waiting for a safe VMBus connection",
        "el controlador Hyper-V esta esperando una conexion segura del bus VMBus");
  case NET_REFRESH_WAIT_OFFER:
    return localization_select(
        language,
        "controlador Hyper-V aguardando cache da offer VMBus desta NIC",
        "Hyper-V controller is waiting for this NIC VMBus offer to be cached",
        "el controlador Hyper-V esta esperando que la offer VMBus de esta NIC quede en cache");
  case NET_REFRESH_FAILED:
    return localization_select(language,
                               "falha ao avancar o runtime de rede Hyper-V",
                               "failed to advance the Hyper-V network runtime",
                               "fallo al avanzar el runtime de red de Hyper-V");
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
                               "destino invalido (use IPv4, hostname, gateway, dns ou self)",
                               "invalid destination (use IPv4, hostname, gateway, dns or self)",
                               "destino invalido (usa IPv4, hostname, gateway, dns o self)");
  case NET_DNS_LOOKUP_FAILED:
    return localization_select(language,
                               "falha ao resolver hostname via DNS",
                               "failed to resolve hostname through DNS",
                               "fallo al resolver el hostname por DNS");
  case NET_NO_REPLY:
  default:
    return localization_select(language, "sem resposta do destino",
                               "no reply from destination",
                               "sin respuesta del destino");
  }
}

const char *net_cli_current_mode(const struct shell_context *ctx) {
  if (ctx && ctx->settings && ctx->settings->network_mode[0]) {
    return shell_string_equal(ctx->settings->network_mode, "dhcp") ? "dhcp"
                                                                    : "static";
  }
  return "static";
}

void net_cli_print_mode_list(const char *language) {
  shell_print(localization_select(language, "Current network mode: ",
                                  "Current network mode: ",
                                  "Modo de red actual: "));
  shell_print(localization_select(language, "use `net-mode show`.\n",
                                  "use `net-mode show`.\n",
                                  "usa `net-mode show`.\n"));
  shell_print(" - static : ");
  shell_print(localization_select(language,
                                  "usa os valores salvos em ipv4/mask/gateway/dns\n",
                                  "uses the saved ipv4/mask/gateway/dns values\n",
                                  "usa los valores guardados en ipv4/mask/gateway/dns\n"));
  shell_print(" - dhcp   : ");
  shell_print(localization_select(language,
                                  "solicita lease dinamico e cai para o estatico salvo se falhar\n",
                                  "requests a dynamic lease and falls back to saved static values on failure\n",
                                  "solicita lease dinamico y cae al estatico guardado si falla\n"));
}

#if defined(__x86_64__)
int net_cli_parse_ipv4(const char *text, uint32_t *out) {
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

int net_cli_read_status(const char *language, struct net_stack_status *out) {
  if (!out) {
    return -1;
  }
  if (net_stack_status(out) != 0) {
    shell_print_error(net_cli_text(language, NET_STACK_UNAVAILABLE));
    return -1;
  }
  return 0;
}

uint32_t net_cli_resolve_target_ip(const char *target,
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

const char *net_cli_runtime_label(const struct net_stack_status *st) {
  if (!st || !st->nic.found) {
    return "none";
  }
  if (st->ready) {
    return "ready";
  }
  if (!st->runtime_supported) {
    return "driver-missing";
  }
  return "init-failed";
}

const char *net_cli_hyperv_stage_label(const struct net_stack_status *st) {
  return net_hyperv_stage_label(st);
}

const char *net_cli_hyperv_bus_label(const struct net_stack_status *st) {
  return net_hyperv_bus_label(st);
}

const char *net_cli_hyperv_runtime_phase_label(
    const struct net_stack_status *st) {
  if (!st || !st->hyperv_runtime_configured) {
    return "none";
  }
  return net_hyperv_runtime_phase_label(st->hyperv_runtime_phase);
}

const char *net_cli_hyperv_block_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform) {
  return net_hyperv_block_label(st, platform);
}

const char *net_cli_hyperv_offer_cache_label(
    const struct net_stack_status *st) {
  return net_hyperv_offer_cache_label(st);
}

const char *net_cli_hyperv_refresh_action_label(
    const struct net_stack_status *st) {
  return st ? net_hyperv_refresh_action_label(st->hyperv_refresh_action)
            : "n/a";
}

const char *net_cli_hyperv_effective_next_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform) {
  return net_hyperv_effective_next_label(st, platform);
}

const char *net_cli_hyperv_gate_label(const struct net_stack_status *st) {
  return st ? net_hyperv_runtime_gate_label(st->hyperv_gate_state) : "invalid";
}

int net_cli_hyperv_platform_blocked(struct system_runtime_platform *platform) {
  return net_hyperv_platform_blocked(platform);
}

void net_cli_print_hex16(uint16_t value) {
  char text[7];
  static const char digits[] = "0123456789ABCDEF";
  text[0] = '0';
  text[1] = 'x';
  text[2] = digits[(value >> 12) & 0xFu];
  text[3] = digits[(value >> 8) & 0xFu];
  text[4] = digits[(value >> 4) & 0xFu];
  text[5] = digits[value & 0xFu];
  text[6] = '\0';
  shell_print(text);
}

void net_cli_print_runtime_block_detail(const struct net_stack_status *st) {
  struct system_runtime_platform platform;
  int32_t value = 0;

  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return;
  }

  net_cli_hyperv_platform_blocked(&platform);
  shell_print("block=");
  shell_print(net_cli_hyperv_block_label(st, &platform));
  shell_print(" ebs=");
  shell_print(system_exit_boot_services_gate_label(
      platform.exit_boot_services_gate));
  shell_print(" input-gate=");
  shell_print(system_hyperv_input_gate_label(platform.hyperv_input_gate));
  if (st->hyperv_last_error != 0) {
    value = st->hyperv_last_error;
    shell_print(" last_error=");
    if (value < 0) {
      shell_print("-");
      shell_print_number((uint32_t)(-value));
    } else {
      shell_print_number((uint32_t)value);
    }
  }
  shell_newline();
}
#endif
