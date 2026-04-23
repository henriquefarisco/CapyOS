#include "internal/network_bootstrap_internal.h"

#include "arch/x86_64/storage_runtime.h"
#include "core/system_init.h"
#include "net/hyperv_platform_diag.h"
#include "net/stack.h"

static const char *net_runtime_state_label(
    const struct net_stack_status *net_status) {
  if (!net_status) {
    return "none";
  }
  if (net_status->ready) {
    return "ready";
  }
  if (!net_status->runtime_supported) {
    return "driver-missing";
  }
  return "init-failed";
}

static void network_bootstrap_print_hyperv_status(
    const struct network_bootstrap_io *io,
    const struct system_runtime_platform *runtime_platform,
    const struct net_stack_status *net_status) {
  struct storvsc_controller_status storvsc_status;

  if (!io || !runtime_platform || !net_status || net_status->ready ||
      net_status->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return;
  }

  if (!net_status->runtime_supported) {
    io->print(
        "[net] Hyper-V synthetic NIC detected; NetVSC runtime backend is still missing.\n");
  } else {
    io->print(
        "[net] Hyper-V synthetic NIC detected; promovendo NetVSC na sequencia conhecida do Linux: offer -> channel -> control -> ready.\n");
  }
  if (net_hyperv_platform_is_blocked(runtime_platform)) {
    io->print(
        "[net] Hyper-V NetVSC blocked by hybrid platform runtime; native-runtime gate still closed.\n");
    io->print("[net] Native runtime gate: ");
    io->print(
        system_exit_boot_services_gate_label(runtime_platform->exit_boot_services_gate));
    io->print(" input-gate=");
    io->print(
        system_hyperv_input_gate_label(runtime_platform->hyperv_input_gate));
    io->putc('\n');
  } else if (!runtime_platform->native_storage_ready) {
    if (runtime_platform->synthetic_storage_ready) {
      io->print(
          "[net] StorVSC sintetico ja esta pronto; o volume ainda pode seguir em fallback EFI sem bloquear a promocao do NetVSC.\n");
    } else {
      io->print(
          "[net] Hyper-V NetVSC aguardando storage nativo antes de promover a rede sintetica.\n");
    }
  }

  if (net_status->hyperv_runtime_configured) {
    io->print("[net] NetVSC controller: ");
    io->print(net_status->hyperv_runtime_enabled ? "enabled" : "disabled");
    io->print(" stage=");
    io->print(net_hyperv_stage_label(net_status));
    io->print(" vmbus=");
    io->print(net_hyperv_bus_label(net_status));
    io->print(" cache=");
    io->print(net_hyperv_offer_cache_label(net_status));
    io->print(" phase=");
    io->print(net_hyperv_runtime_phase_label(net_status->hyperv_runtime_phase));
    io->print(" next=");
    io->print(net_hyperv_refresh_action_label(
        net_status->hyperv_refresh_action));
    io->print(" gate=");
    io->print(net_hyperv_runtime_gate_label(net_status->hyperv_gate_state));
    io->print(" block=");
    io->print(net_hyperv_block_label(net_status, runtime_platform));
    io->putc('\n');
  }

  if (x64_storage_runtime_hyperv_controller_status(&storvsc_status) == 0) {
    io->print("[net] StorVSC controller: ");
    io->print(storvsc_status.enabled ? "enabled" : "disabled");
    io->print(" stage=");
    io->print(hyperv_vmbus_stage_label(storvsc_status.stage));
    io->print(" phase=");
    io->print(x64_storage_runtime_hyperv_phase_name());
    io->print(" vmbus=");
    io->print(hyperv_vmbus_stage_label(storvsc_status.vmbus_stage));
    io->print(" cache=");
    io->print(net_hyperv_storage_cache_label());
    io->print(" gate=");
    io->print(net_hyperv_storage_gate_label(runtime_platform));
    io->print(" next=");
    io->print(net_hyperv_storage_next_label(runtime_platform));
    io->print(" block=");
    io->print(net_hyperv_storage_block_label(runtime_platform));
    io->putc('\n');
  }

  if (net_status->nic.vmbus_relid != 0u ||
      net_status->nic.vmbus_connection_id != 0u) {
    io->print("[net] VMBus offer: relid=");
    io->print_dec_u32(net_status->nic.vmbus_relid);
    io->print(" conn=");
    io->print_dec_u32(net_status->nic.vmbus_connection_id);
    io->print(" dedicated=");
    io->print(net_status->nic.vmbus_dedicated_interrupt ? "yes" : "no");
    io->putc('\n');
  }
}

void network_bootstrap_print_status(
    const struct network_bootstrap_io *io,
    const struct system_runtime_platform *runtime_platform,
    const struct net_stack_status *net_status) {
  if (!io || !net_status) {
    return;
  }

  if (net_status->nic.found) {
    io->print("[net] NIC: ");
    io->print(net_driver_name(net_status->nic.kind));
    io->print(" @ ");
    io->print_dec_u32((uint32_t)net_status->nic.bus);
    io->putc(':');
    io->print_dec_u32((uint32_t)net_status->nic.device);
    io->putc('.');
    io->print_dec_u32((uint32_t)net_status->nic.function);
    io->print(" vendor=");
    io->print_hex16(net_status->nic.vendor_id);
    io->print(" device=");
    io->print_hex16(net_status->nic.device_id);
    io->putc('\n');
    io->print("[net] Runtime: ");
    io->print(net_runtime_state_label(net_status));
    io->putc('\n');
    network_bootstrap_print_hyperv_status(io, runtime_platform, net_status);
  } else {
    io->print("[net] No known NIC detected.\n");
  }

  io->print("[net] MAC: ");
  io->print_mac(net_status->ipv4.mac);
  io->print("  IPv4: ");
  io->print_ipv4(net_status->ipv4.addr);
  io->print("  GW: ");
  io->print_ipv4(net_status->ipv4.gateway);
  io->putc('\n');
}

void network_bootstrap_print_selftest(const struct network_bootstrap_io *io,
                                      int ok) {
  if (!io) {
    return;
  }
  if (ok) {
    io->print("[net] Protocol self-test (ARP/IPv4/ICMP/UDP/TCP): OK\n");
  } else {
    io->print("[net] Protocol self-test (ARP/IPv4/ICMP/UDP/TCP): FAILED\n");
  }
}
