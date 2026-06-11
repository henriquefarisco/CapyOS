#include "net/internal/stack_arp.h"

#include <stdio.h>
#include <string.h>

/*
 * Host coverage for the ARP receive path (net_arp_handle) and cache
 * management (net_arp_update / find / count). ARP packets are untrusted
 * network input that drive the cache used to route every frame, so the
 * safety properties are: short frames and malformed header fields are
 * rejected before use, cache slot selection stays in bounds (including the
 * age-based eviction when full), and a reply is only emitted for an ARP
 * request that targets our own address. stack_arp.c is self-contained
 * (local static helpers + a send callback), so the test links it directly.
 */

static int g_send_count;
static uint16_t g_send_len;
static uint8_t g_send_frame[64];

static int test_send_frame(const uint8_t *frame, uint16_t len) {
  g_send_count++;
  g_send_len = len;
  if (frame && len <= sizeof(g_send_frame)) {
    memcpy(g_send_frame, frame, len);
  }
  return 0;
}

static void reset_capture(void) {
  g_send_count = 0;
  g_send_len = 0;
  memset(g_send_frame, 0, sizeof(g_send_frame));
}

/* Build a 28-byte ARP packet (Ethernet/IPv4) in network byte order. */
static size_t build_arp(uint8_t *p, uint16_t opcode, const uint8_t sender_mac[6],
                        uint32_t sender_ip, uint32_t target_ip) {
  size_t i;
  p[0] = 0x00u; p[1] = 0x01u;  /* htype = Ethernet */
  p[2] = 0x08u; p[3] = 0x00u;  /* ptype = IPv4 */
  p[4] = 6u;    p[5] = 4u;     /* hlen, plen */
  p[6] = (uint8_t)(opcode >> 8); p[7] = (uint8_t)(opcode & 0xFFu);
  for (i = 0; i < 6u; ++i) p[8u + i] = sender_mac[i];
  p[14] = (uint8_t)(sender_ip >> 24); p[15] = (uint8_t)(sender_ip >> 16);
  p[16] = (uint8_t)(sender_ip >> 8);  p[17] = (uint8_t)(sender_ip & 0xFFu);
  for (i = 0; i < 6u; ++i) p[18u + i] = 0u; /* target_mac unknown in request */
  p[24] = (uint8_t)(target_ip >> 24); p[25] = (uint8_t)(target_ip >> 16);
  p[26] = (uint8_t)(target_ip >> 8);  p[27] = (uint8_t)(target_ip & 0xFFu);
  return 28u;
}

int run_net_arp_tests(void) {
  int fails = 0;
  const uint8_t sender_mac[6] = {0x02u, 0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu};
  struct net_ipv4_config ipv4;

  memset(&ipv4, 0, sizeof(ipv4));
  ipv4.addr = 0x0A000001u; /* 10.0.0.1 (us) */
  ipv4.mask = 0xFFFFFF00u;
  ipv4.gateway = 0x0A0000FEu;
  ipv4.mac[0] = 0x52u; ipv4.mac[1] = 0x54u; ipv4.mac[2] = 0x00u;
  ipv4.mac[3] = 0x11u; ipv4.mac[4] = 0x22u; ipv4.mac[5] = 0x33u;

  /* Request targeting us, ready -> cache learns sender + a REPLY is sent. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    size_t plen = build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                            0x0A000001u);
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    int idx;
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, pkt, plen, 1,
                       test_send_frame) != 0 ||
        g_send_count != 1 || g_send_len != 42u ||
        g_send_frame[20] != 0x00u || g_send_frame[21] != 0x02u /* REPLY */ ||
        g_send_frame[0] != sender_mac[0] /* reply unicast to requester */) {
      printf("[arp] request-for-us did not produce a correct reply\n");
      fails++;
    }
    idx = net_arp_find(entries, 8u, 0x0A000002u);
    if (idx < 0 || memcmp(entries[idx].mac, sender_mac, 6) != 0) {
      printf("[arp] sender not learned into the cache\n");
      fails++;
    }
  }

  /* Same request but not ready -> cache learns sender, but NO reply. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    size_t plen = build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                            0x0A000001u);
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, pkt, plen, 0,
                       test_send_frame) != 0 ||
        g_send_count != 0 || net_arp_find(entries, 8u, 0x0A000002u) < 0) {
      printf("[arp] not-ready request mishandled\n");
      fails++;
    }
  }

  /* Request targeting a different host -> learn sender, but no reply. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    size_t plen = build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                            0x0A0000AAu);
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, pkt, plen, 1,
                       test_send_frame) != 0 ||
        g_send_count != 0 || net_arp_find(entries, 8u, 0x0A000002u) < 0) {
      printf("[arp] request for another host produced a reply\n");
      fails++;
    }
  }

  /* Short frame (< 28 bytes) -> rejected before any field is read. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    (void)build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                    0x0A000001u);
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, pkt, 20u, 1,
                       test_send_frame) != -1 ||
        g_send_count != 0 || net_arp_count(entries, 8u) != 0u ||
        stats.arp_seen != 0u) {
      printf("[arp] short frame not rejected\n");
      fails++;
    }
  }

  /* Malformed header (bad htype) -> rejected, cache untouched. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    size_t plen = build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                            0x0A000001u);
    pkt[0] = 0x00u; pkt[1] = 0x99u; /* htype no longer Ethernet */
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, pkt, plen, 1,
                       test_send_frame) != -1 ||
        g_send_count != 0 || net_arp_count(entries, 8u) != 0u) {
      printf("[arp] malformed htype not rejected\n");
      fails++;
    }
  }

  /* NULL payload / NULL entries -> rejected. */
  {
    struct net_arp_entry entries[8];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t pkt[28];
    size_t plen = build_arp(pkt, NET_ARP_OP_REQUEST, sender_mac, 0x0A000002u,
                            0x0A000001u);
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_arp_handle(entries, 8u, &age, &stats, &ipv4, NULL, plen, 1,
                       test_send_frame) != -1 ||
        net_arp_handle(NULL, 8u, &age, &stats, &ipv4, pkt, plen, 1,
                       test_send_frame) != -1 ||
        g_send_count != 0) {
      printf("[arp] NULL inputs not rejected\n");
      fails++;
    }
  }

  /* Cache eviction stays in bounds: fill a small table past capacity. */
  {
    struct net_arp_entry entries[4];
    uint32_t age = 0;
    struct net_stack_stats stats;
    uint8_t mac[6] = {0x10u, 0, 0, 0, 0, 0};
    uint32_t i;
    memset(entries, 0, sizeof(entries));
    memset(&stats, 0, sizeof(stats));
    for (i = 0; i < 10u; ++i) {
      mac[5] = (uint8_t)i;
      net_arp_update(entries, 4u, &age, &stats, 0x0A000000u + i, mac);
    }
    /* Never more valid entries than capacity; no out-of-bounds write. */
    if (net_arp_count(entries, 4u) != 4u) {
      printf("[arp] eviction exceeded capacity (count=%u)\n",
             net_arp_count(entries, 4u));
      fails++;
    }
  }

  /* Pure routing helper sanity. */
  {
    if (net_arp_route_next_hop(0x0A000001u, 0xFFFFFF00u, 0x0A0000FEu,
                               0x0A000005u) != 0x0A000005u /* same subnet */ ||
        net_arp_route_next_hop(0x0A000001u, 0xFFFFFF00u, 0x0A0000FEu,
                               0x08080808u) != 0x0A0000FEu /* via gateway */) {
      printf("[arp] route_next_hop wrong\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] net_arp OK\n");
  }
  return fails;
}
