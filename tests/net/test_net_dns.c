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
    uint32_t out_ttl = 0;
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

    if (net_dns_parse_first_a(packet, offset, 0x1234u, &out_ip, &out_ttl) != 0 ||
        out_ip != 0x5DB8D822u || out_ttl != 60u) {
      printf("[dns] failed to parse compressed A answer (ttl=%u)\n", out_ttl);
      fails++;
    }
  }

  {
    uint8_t invalid[16];
    uint32_t out_ip = 0;
    memset(invalid, 0, sizeof(invalid));
    write_be16(&invalid[0], 0x9999u);
    write_be16(&invalid[2], 0x8183u);
    if (net_dns_parse_first_a(invalid, sizeof(invalid), 0x9999u, &out_ip, NULL) == 0) {
      printf("[dns] parser should reject DNS error replies\n");
      fails++;
    }
  }

  /* Sessao 44: NXDOMAIN response with SOA in authority section.
   * Layout: header(RCODE=3, QD=1, AN=0, NS=1, AR=0) +
   *         question(name=example.com,A,IN) +
   *         authority(name=ptr-to-question, TYPE=SOA, CLASS=IN,
   *                   TTL=900, RDLEN=variable) {
   *           MNAME=ns1.example.com (inline)
   *           RNAME=admin.example.com (inline)
   *           SERIAL=1, REFRESH=3600, RETRY=600, EXPIRE=86400,
   *           MINIMUM=60
   *         }
   * Expected neg_ttl = min(SOA.TTL=900, SOA.MINIMUM=60) = 60. */
  {
    uint8_t packet[256];
    size_t offset = 0;
    uint32_t out_neg_ttl = 0;
    memset(packet, 0, sizeof(packet));

    write_be16(&packet[0], 0x4321u);  /* id */
    write_be16(&packet[2], 0x8183u);  /* flags: QR=1, RA=1, RCODE=3 NXDOMAIN */
    write_be16(&packet[4], 1u);       /* QDCOUNT */
    write_be16(&packet[6], 0u);       /* ANCOUNT */
    write_be16(&packet[8], 1u);       /* NSCOUNT */
    write_be16(&packet[10], 0u);      /* ARCOUNT */
    offset = 12u;

    /* Question: example.com A IN. */
    packet[offset++] = 7u;
    memcpy(&packet[offset], "example", 7u); offset += 7u;
    packet[offset++] = 3u;
    memcpy(&packet[offset], "com", 3u); offset += 3u;
    packet[offset++] = 0u;
    write_be16(&packet[offset], 1u); offset += 2u;  /* TYPE=A */
    write_be16(&packet[offset], 1u); offset += 2u;  /* CLASS=IN */

    /* Authority: SOA. NAME = pointer to offset 12 (example.com). */
    packet[offset++] = 0xC0u; packet[offset++] = 0x0Cu;
    write_be16(&packet[offset], 6u); offset += 2u;  /* TYPE=SOA */
    write_be16(&packet[offset], 1u); offset += 2u;  /* CLASS=IN */
    write_be32(&packet[offset], 900u); offset += 4u; /* SOA RR TTL */
    /* RDLEN placeholder (we patch after writing RDATA). */
    size_t rdlen_off = offset;
    offset += 2u;
    size_t rdata_start = offset;

    /* MNAME = "ns1.example.com" inline (no compression for simplicity). */
    packet[offset++] = 3u; memcpy(&packet[offset], "ns1", 3u); offset += 3u;
    packet[offset++] = 7u; memcpy(&packet[offset], "example", 7u); offset += 7u;
    packet[offset++] = 3u; memcpy(&packet[offset], "com", 3u); offset += 3u;
    packet[offset++] = 0u;
    /* RNAME = "admin.example.com" inline. */
    packet[offset++] = 5u; memcpy(&packet[offset], "admin", 5u); offset += 5u;
    packet[offset++] = 7u; memcpy(&packet[offset], "example", 7u); offset += 7u;
    packet[offset++] = 3u; memcpy(&packet[offset], "com", 3u); offset += 3u;
    packet[offset++] = 0u;
    write_be32(&packet[offset], 1u);     offset += 4u; /* SERIAL */
    write_be32(&packet[offset], 3600u);  offset += 4u; /* REFRESH */
    write_be32(&packet[offset], 600u);   offset += 4u; /* RETRY */
    write_be32(&packet[offset], 86400u); offset += 4u; /* EXPIRE */
    write_be32(&packet[offset], 60u);    offset += 4u; /* MINIMUM */

    /* Patch RDLEN. */
    write_be16(&packet[rdlen_off], (uint16_t)(offset - rdata_start));

    if (net_dns_parse_negative_ttl(packet, offset, 0x4321u, &out_neg_ttl) != 0 ||
        out_neg_ttl != 60u) {
      printf("[dns] failed to parse NXDOMAIN+SOA negative TTL (got=%u, want=60)\n",
             out_neg_ttl);
      fails++;
    }

    /* Same packet, but probed via positive parser: must return -1. */
    {
      uint32_t pos_ip = 0xDEADBEEFu;
      uint32_t pos_ttl = 999u;
      if (net_dns_parse_first_a(packet, offset, 0x4321u, &pos_ip, &pos_ttl) == 0) {
        printf("[dns] positive parser accepted NXDOMAIN response\n");
        fails++;
      }
    }
  }

  /* Sessao 44: SERVFAIL (RCODE=2) is NOT a definitive negative.
   * Even with a SOA in authority, the negative parser must
   * reject it -- RCODE=2 means upstream had trouble, retry. */
  {
    uint8_t packet[32];
    uint32_t out_neg_ttl = 99u;
    memset(packet, 0, sizeof(packet));
    write_be16(&packet[0], 0x5555u);
    write_be16(&packet[2], 0x8182u);  /* RCODE=2 SERVFAIL */
    write_be16(&packet[4], 0u);
    write_be16(&packet[6], 0u);
    write_be16(&packet[8], 1u);  /* NSCOUNT=1 (irrelevant) */
    write_be16(&packet[10], 0u);
    if (net_dns_parse_negative_ttl(packet, sizeof(packet), 0x5555u, &out_neg_ttl) == 0) {
      printf("[dns] negative parser must reject SERVFAIL (cached=transient)\n");
      fails++;
    }
  }

  /* Sessao 44: NXDOMAIN with NSCOUNT=0 (no SOA) -> reject.
   * Without SOA we have no negative TTL hint and refuse to
   * make one up. Caller treats as transient and re-queries. */
  {
    uint8_t packet[32];
    uint32_t out_neg_ttl = 99u;
    memset(packet, 0, sizeof(packet));
    write_be16(&packet[0], 0x6666u);
    write_be16(&packet[2], 0x8183u);  /* QR=1, RCODE=3 NXDOMAIN */
    write_be16(&packet[4], 0u);
    write_be16(&packet[6], 0u);
    write_be16(&packet[8], 0u);  /* NSCOUNT=0 */
    write_be16(&packet[10], 0u);
    if (net_dns_parse_negative_ttl(packet, sizeof(packet), 0x6666u, &out_neg_ttl) == 0) {
      printf("[dns] negative parser accepted NXDOMAIN with no SOA\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] net_dns OK\n");
  }
  return fails;
}
