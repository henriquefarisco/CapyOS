#include "drivers/net/e1000.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message) {
  if (condition) {
    return 0;
  }
  printf("[e1000-identity] %s\n", message);
  return 1;
}

int run_e1000_identity_tests(void) {
  static const uint8_t vmware_mac[6] = {0x00u, 0x0Cu, 0x29u,
                                         0x9Eu, 0x06u, 0x87u};
  static const uint8_t multicast_mac[6] = {0x01u, 0x00u, 0x5Eu,
                                            0x00u, 0x00u, 0x01u};
  static const uint8_t zero_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};
  uint8_t decoded[6] = {0u};
  uint32_t ral = 0u;
  uint32_t rah = 0u;
  int failures = 0;

  failures += expect(e1000_mac_encode(vmware_mac, &ral, &rah) == 0,
                     "VMware unicast MAC was rejected");
  failures += expect(ral == 0x9E290C00u,
                     "RAL byte order does not match E1000 wire order");
  failures += expect((rah & 0x0000FFFFu) == 0x00008706u,
                     "RAH address bytes are wrong");
  failures += expect((rah & 0x80000000u) != 0u,
                     "RAH address-valid bit is missing");
  failures += expect(e1000_mac_decode(ral, rah, decoded) == 0,
                     "encoded MAC could not be decoded");
  failures += expect(memcmp(decoded, vmware_mac, sizeof(decoded)) == 0,
                     "E1000 MAC register round-trip changed identity");
  failures += expect(e1000_mac_encode(multicast_mac, &ral, &rah) != 0,
                     "multicast station identity must be rejected");
  failures += expect(e1000_mac_encode(zero_mac, &ral, &rah) != 0,
                     "zero station identity must be rejected");
  failures += expect(e1000_mac_decode(0u, 0u, decoded) != 0,
                     "empty receive-address registers must be rejected");

  if (failures == 0) {
    printf("[tests] e1000_identity OK\n");
  }
  return failures;
}
