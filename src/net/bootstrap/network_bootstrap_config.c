#include "internal/network_bootstrap_internal.h"

#include "core/system_init.h"
#include "net/stack.h"

int network_bootstrap_io_ready(const struct network_bootstrap_io *io) {
  return io && io->print && io->print_dec_u32 && io->print_hex16 &&
         io->print_ipv4 && io->print_mac && io->putc;
}

static const char *boot_network_mode(const struct system_settings *settings) {
  if (settings && settings->network_mode[0] == 'd' &&
      settings->network_mode[1] == 'h' && settings->network_mode[2] == 'c' &&
      settings->network_mode[3] == 'p' && settings->network_mode[4] == '\0') {
    return "dhcp";
  }
  return "static";
}

void network_bootstrap_apply_settings(const struct network_bootstrap_io *io,
                                      const struct system_settings *settings) {
  const char *mode = boot_network_mode(settings);

  if (!io || !settings) {
    return;
  }

  (void)net_stack_set_ipv4(settings->ipv4_addr, settings->ipv4_mask,
                           settings->ipv4_gateway, settings->ipv4_dns);
  io->print("[net] Mode: ");
  io->print(mode);
  io->putc('\n');
  if (mode[0] == 'd' && net_stack_ready()) {
    if (net_stack_dhcp_acquire(2500u) == 0) {
      io->print("[net] DHCP: lease acquired.\n");
    } else {
      io->print("[net] DHCP: failed, keeping saved static fallback.\n");
    }
  }
}
