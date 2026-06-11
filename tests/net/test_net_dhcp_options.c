#include "net/dhcp_options.h"

#include <stdio.h>

/* DHCP option wire codes (the parser TU keeps its own copies; these mirror
 * the RFC 2132 values for test packets). */
#define OPT_SUBNET 1u
#define OPT_ROUTER 3u
#define OPT_DNS 6u
#define OPT_MSG_TYPE 53u
#define OPT_SERVER_ID 54u
#define OPT_END 255u

int run_net_dhcp_options_tests(void) {
  int fails = 0;

  /* Happy path: an OFFER carrying server-id, subnet, router and DNS. */
  {
    uint8_t msg = 0u;
    uint32_t sid = 0u, mask = 0u, rtr = 0u, dns = 0u;
    static const uint8_t opts[] = {
        OPT_MSG_TYPE,  1u, 2u,                       /* OFFER */
        OPT_SERVER_ID, 4u, 192u, 168u, 1u, 1u,
        OPT_SUBNET,    4u, 255u, 255u, 255u, 0u,
        OPT_ROUTER,    4u, 192u, 168u, 1u, 254u,
        OPT_DNS,       4u, 8u, 8u, 8u, 8u,
        OPT_END};
    dhcp_parse_options(opts, sizeof(opts), &msg, &sid, &mask, &rtr, &dns);
    if (msg != 2u || sid != 0xC0A80101u || mask != 0xFFFFFF00u ||
        rtr != 0xC0A801FEu || dns != 0x08080808u) {
      printf("[dhcp-opts] happy-path extraction wrong\n");
      fails++;
    }
  }

  /* === Adversarial / malformed-packet robustness ====================
   * A DHCP OFFER/ACK is untrusted network input. The parser must never read
   * past `len`, never loop, and must skip malformed options fail-safe,
   * leaving out-params untouched. These cases lock that behaviour. */

  /* (a) NULL option block -> no read, outputs untouched. */
  {
    uint8_t msg = 0xAAu;
    uint32_t sid = 0xDEADu;
    dhcp_parse_options(NULL, 16u, &msg, &sid, NULL, NULL, NULL);
    if (msg != 0xAAu || sid != 0xDEADu) {
      printf("[dhcp-opts] NULL options mutated outputs\n");
      fails++;
    }
  }

  /* (b) Empty block (len 0) -> nothing parsed. */
  {
    uint8_t msg = 0xAAu;
    static const uint8_t opts[] = {OPT_MSG_TYPE, 1u, 2u};
    dhcp_parse_options(opts, 0u, &msg, NULL, NULL, NULL, NULL);
    if (msg != 0xAAu) {
      printf("[dhcp-opts] len=0 parsed data\n");
      fails++;
    }
  }

  /* (c) Code byte with no following length byte (idx == len) -> stop. */
  {
    uint8_t msg = 0xAAu;
    static const uint8_t opts[] = {OPT_MSG_TYPE};
    dhcp_parse_options(opts, sizeof(opts), &msg, NULL, NULL, NULL, NULL);
    if (msg != 0xAAu) {
      printf("[dhcp-opts] truncated length byte parsed\n");
      fails++;
    }
  }

  /* (d) opt_len claims more bytes than remain -> stop, field untouched. */
  {
    uint32_t sid = 0xDEADu;
    static const uint8_t opts[] = {OPT_SERVER_ID, 4u, 192u, 168u};
    dhcp_parse_options(opts, sizeof(opts), NULL, &sid, NULL, NULL, NULL);
    if (sid != 0xDEADu) {
      printf("[dhcp-opts] over-long option read out of bounds\n");
      fails++;
    }
  }

  /* (e) Field too short for its type (server-id len 2) -> skipped; a valid
   * option after it is still parsed. */
  {
    uint8_t msg = 0u;
    uint32_t sid = 0xDEADu;
    static const uint8_t opts[] = {OPT_SERVER_ID, 2u, 1u, 2u,
                                   OPT_MSG_TYPE,   1u, 5u, OPT_END};
    dhcp_parse_options(opts, sizeof(opts), &msg, &sid, NULL, NULL, NULL);
    if (sid != 0xDEADu || msg != 5u) {
      printf("[dhcp-opts] short-field handling wrong\n");
      fails++;
    }
  }

  /* (f) No END marker; block ends exactly at len -> stop cleanly. */
  {
    uint8_t msg = 0u;
    static const uint8_t opts[] = {OPT_MSG_TYPE, 1u, 3u};
    dhcp_parse_options(opts, sizeof(opts), &msg, NULL, NULL, NULL, NULL);
    if (msg != 3u) {
      printf("[dhcp-opts] missing END mishandled\n");
      fails++;
    }
  }

  /* (g) PAD (0) bytes skipped; bytes after END are ignored. */
  {
    uint8_t msg = 0u;
    uint32_t sid = 0xDEADu;
    static const uint8_t opts[] = {0u,           0u,  /* pad */
                                   OPT_MSG_TYPE,  1u, 2u,
                                   OPT_END,
                                   OPT_SERVER_ID, 4u, 1u, 2u, 3u, 4u};
    dhcp_parse_options(opts, sizeof(opts), &msg, &sid, NULL, NULL, NULL);
    if (msg != 2u || sid != 0xDEADu) {
      printf("[dhcp-opts] pad/END handling wrong\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] net_dhcp_options OK\n");
  }
  return fails;
}
