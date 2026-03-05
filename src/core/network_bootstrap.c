#include "core/network_bootstrap.h"

#include "net/stack.h"

static int io_ready(const struct network_bootstrap_io *io) {
  return io && io->print && io->print_dec_u32 && io->print_hex16 &&
         io->print_ipv4 && io->print_mac && io->putc;
}

void network_bootstrap_run(const struct network_bootstrap_io *io) {
  struct net_stack_status net_status;
  int net_rc = 0;

  if (!io_ready(io)) {
    return;
  }

  io->print("[net] Inicializando stack TCP/IP...\n");
  net_rc = net_stack_init();
  if (net_stack_status(&net_status) == 0) {
    if (net_rc == 0 && net_status.nic.found) {
      io->print("[net] NIC: ");
      io->print(net_driver_name(net_status.nic.kind));
      io->print(" @ ");
      io->print_dec_u32((uint32_t)net_status.nic.bus);
      io->putc(':');
      io->print_dec_u32((uint32_t)net_status.nic.device);
      io->putc('.');
      io->print_dec_u32((uint32_t)net_status.nic.function);
      io->print(" vendor=");
      io->print_hex16(net_status.nic.vendor_id);
      io->print(" device=");
      io->print_hex16(net_status.nic.device_id);
      io->putc('\n');
    } else {
      io->print(
          "[net] Nenhuma NIC suportada detectada "
          "(e1000/tulip/rtl8139/virtio/hyperv).\n");
    }
    io->print("[net] MAC: ");
    io->print_mac(net_status.ipv4.mac);
    io->print("  IPv4: ");
    io->print_ipv4(net_status.ipv4.addr);
    io->print("  GW: ");
    io->print_ipv4(net_status.ipv4.gateway);
    io->putc('\n');
  } else {
    io->print("[net] Falha ao consultar estado da stack.\n");
  }

  if (net_stack_protocol_selftest() == 0) {
    io->print("[net] Self-test de protocolos (ARP/IPv4/ICMP/UDP/TCP): OK\n");
  } else {
    io->print(
        "[net] Self-test de protocolos (ARP/IPv4/ICMP/UDP/TCP): FALHOU\n");
  }
}
