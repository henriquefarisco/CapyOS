#include "net/network_bootstrap.h"

#include "internal/network_bootstrap_internal.h"

#include "kernel/log/klog.h"
#include "core/system_init.h"
#include "net/stack.h"

static void network_bootstrap_try_runtime_budget(
    const struct network_bootstrap_io *io,
    const struct system_runtime_platform *runtime_platform) {
  uint32_t round = 0u;
  uint32_t idle_rounds = 0u;
  struct net_stack_status net_status;

  if (!io || !runtime_platform) {
    return;
  }
  if (runtime_platform->boot_services_active || runtime_platform->hybrid_boot) {
    return;
  }
  if (net_stack_ready()) {
    return;
  }

  io->print(
      "[net] Budget NetVSC: aplicando algumas iteracoes controladas de bring-up antes de entregar o login.\n");
  for (round = 0u; round < 24u; ++round) {
    int rc = net_stack_refresh_runtime();

    if (net_stack_ready()) {
      io->print("[net] Budget NetVSC concluiu o handshake sintetico antes da interacao do usuario.\n");
      return;
    }
    if (rc < 0) {
      io->print(
          "[net] Budget NetVSC encontrou erro; mantendo fallback de rede atual e preservando diagnostico detalhado.\n");
      return;
    }
    if (rc > 0) {
      idle_rounds = 0u;
      continue;
    }
    idle_rounds += 1u;
    if (idle_rounds >= 8u) {
      break;
    }
  }

  if (net_stack_status(&net_status) == 0 &&
      net_status.nic.kind == NET_NIC_KIND_HYPERV_NETVSC && !net_status.ready) {
    io->print(
        "[net] Budget NetVSC esgotado; o boot segue com o stack em modo diagnostico ate a proxima janela de promocao.\n");
    network_bootstrap_print_status(io, runtime_platform, &net_status);
  }
}

void network_bootstrap_run(const struct network_bootstrap_io *io,
                           const struct system_settings *settings) {
  struct net_stack_status net_status;
  struct system_runtime_platform runtime_platform;

  if (!network_bootstrap_io_ready(io)) {
    return;
  }

  io->print("[net] Initializing TCP/IP stack...\n");
  (void)net_stack_init();
  system_runtime_platform_get(&runtime_platform);
  network_bootstrap_try_runtime_budget(io, &runtime_platform);
  network_bootstrap_apply_settings(io, settings);
  if (net_stack_status(&net_status) == 0) {
    network_bootstrap_print_status(io, &runtime_platform, &net_status);
  } else {
    io->print("[net] Failed to read stack state.\n");
  }

  network_bootstrap_print_selftest(io, net_stack_protocol_selftest() == 0);

  /* Mensagem clara quando rede não está disponível */
  if (!net_stack_driver_available()) {
    io->print("\n[net] AVISO: Nenhum driver de rede compativel encontrado.\n");
    io->print("[net] O sistema continuara sem conectividade de rede.\n");
    const char *reason = net_stack_unavailable_reason();
    if (reason) {
      io->print("[net] Motivo: ");
      io->print(reason);
      io->print("\n");
    }
    klog(KLOG_WARN, "[net] Bootstrap: no driver available.");
  } else if (!net_stack_ready()) {
    io->print("\n[net] AVISO: Driver encontrado mas handshake ainda incompleto.\n");
    io->print("[net] O stack tentara completar a conexao em background.\n");
    klog(KLOG_INFO, "[net] Bootstrap: driver available but not ready yet.");
  }
}
