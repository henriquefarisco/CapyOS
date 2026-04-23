#ifndef CORE_NETWORK_BOOTSTRAP_H
#define CORE_NETWORK_BOOTSTRAP_H

#include <stdint.h>

struct system_settings;

struct network_bootstrap_io {
  void (*print)(const char *s);
  void (*print_dec_u32)(uint32_t v);
  void (*print_hex16)(uint16_t v);
  void (*print_ipv4)(uint32_t ip);
  void (*print_mac)(const uint8_t mac[6]);
  void (*putc)(char c);
};

void network_bootstrap_run(const struct network_bootstrap_io *io,
                           const struct system_settings *settings);

#endif /* CORE_NETWORK_BOOTSTRAP_H */
