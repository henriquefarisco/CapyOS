#include <stdio.h>
#include <string.h>

#include "net/dns.h"

static void write_be16(uint8_t *ptr, uint16_t value) {
  ptr[0] = (uint8_t)((value >> 8) & 0xFFu);
  ptr[1] = (uint8_t)(value & 0xFFu);
}

static void write_be32(uint8_t *ptr, uint32_t value) {
  ptr[0] = (uint8_t)((value >> 24) & 0xFFu);
  ptr[1] = (uint8_t)((value >> 16) & 0xFFu);
  ptr[2] = (uint8_t)((value >> 8) & 0xFFu);
  ptr[3] = (uint8_t)(value & 0xFFu);
}

int run_net_dns_tests(void) {
  int fails = 0;

  {
    uint8_t encoded[64];
    size_t encoded_len = 0;
    static const uint8_t expected[] = {7u, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
                                       3u, 'c', 'o', 'm', 0u};
    memset(encoded, 0, sizeof(encoded));
    if (net_dns_encode_name("example.com", encoded, sizeof(encoded),
                            &encoded_len) != 0 ||
        encoded_len != sizeof(expected) ||
        memcmp(encoded, expected, sizeof(expected)) != 0) {
      printf("[dns] name encoder failed for example.com\n");
      fails++;
    }
  }

  {
    uint8_t packet[128];
    size_t offset = 0;
    uint32_t out_ip = 0;
    memset(packet, 0, sizeof(packet));

    write_be16(&packet[0], 0x1234u);
    write_be16(&packet[2], 0x8180u);
    write_be16(&packet[4], 1u);
    write_be16(&packet[6], 1u);
    write_be16(&packet[8], 0u);
    write_be16(&packet[10], 0u);
    offset = 12u;
    packet[offset++] = 7u;
    memcpy(&packet[offset], "example", 7u);
    offset += 7u;
    packet[offset++] = 3u;
    memcpy(&packet[offset], "com", 3u);
    offset += 3u;
    packet[offset++] = 0u;
    write_be16(&packet[offset], 1u);
    offset += 2u;
    write_be16(&packet[offset], 1u);
    offset += 2u;

    packet[offset++] = 0xC0u;
    packet[offset++] = 0x0Cu;
    write_be16(&packet[offset], 1u);
    offset += 2u;
    write_be16(&packet[offset], 1u);
    offset += 2u;
    write_be32(&packet[offset], 60u);
    offset += 4u;
    write_be16(&packet[offset], 4u);
    offset += 2u;
    packet[offset++] = 93u;
    packet[offset++] = 184u;
    packet[offset++] = 216u;
    packet[offset++] = 34u;

    if (net_dns_parse_first_a(packet, offset, 0x1234u, &out_ip) != 0 ||
        out_ip != 0x5DB8D822u) {
      printf("[dns] failed to parse compressed A answer\n");
      fails++;
    }
  }

  {
    uint8_t invalid[16];
    uint32_t out_ip = 0;
    memset(invalid, 0, sizeof(invalid));
    write_be16(&invalid[0], 0x9999u);
    write_be16(&invalid[2], 0x8183u);
    if (net_dns_parse_first_a(invalid, sizeof(invalid), 0x9999u, &out_ip) == 0) {
      printf("[dns] parser should reject DNS error replies\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] net_dns OK\n");
  }
  return fails;
}
