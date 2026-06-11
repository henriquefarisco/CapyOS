#include "net/internal/stack_icmp.h"

#include <stdio.h>
#include <string.h>

/*
 * Host coverage for net_icmp_handle(), the ICMP receive path. ICMP echo
 * requests are untrusted network input that get copied into a fixed reply
 * buffer, so the key safety property is that an oversized request is rejected
 * before the copy (len > 256) and a short frame is rejected before the header
 * is read. stack_icmp.c is self-contained (local static helpers + a send
 * callback), so the test links it directly like the DNS parser.
 */

static int g_send_count;
static uint8_t g_send_proto;
static uint32_t g_send_dst;
static size_t g_send_len;
static uint8_t g_send_payload[512];

static int test_send_ipv4(uint8_t protocol, uint32_t dst_ip,
                          const uint8_t *payload, size_t payload_len) {
  g_send_count++;
  g_send_proto = protocol;
  g_send_dst = dst_ip;
  g_send_len = payload_len;
  if (payload && payload_len <= sizeof(g_send_payload)) {
    memcpy(g_send_payload, payload, payload_len);
  }
  return 0;
}

static void reset_capture(void) {
  g_send_count = 0;
  g_send_proto = 0;
  g_send_dst = 0;
  g_send_len = 0;
  memset(g_send_payload, 0, sizeof(g_send_payload));
}

/* Lay down an ICMP header (type/code/checksum/ident/seq) + `data_len` payload
 * bytes. ident/seq are written big-endian (network order). */
static size_t build_icmp(uint8_t *buf, uint8_t type, uint16_t ident,
                         uint16_t seq, size_t data_len) {
  buf[0] = type;
  buf[1] = 0u;
  buf[2] = 0u;
  buf[3] = 0u;
  buf[4] = (uint8_t)(ident >> 8);
  buf[5] = (uint8_t)(ident & 0xFFu);
  buf[6] = (uint8_t)(seq >> 8);
  buf[7] = (uint8_t)(seq & 0xFFu);
  for (size_t i = 0; i < data_len; ++i) {
    buf[8u + i] = (uint8_t)(0x40u + i);
  }
  return 8u + data_len;
}

int run_net_icmp_tests(void) {
  int fails = 0;

  /* Happy path: echo request addressed to us -> echo reply built + sent,
   * type flipped to 0, payload echoed, destined back to the source. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t req[64];
    size_t req_len = build_icmp(req, 8u, 0x1234u, 0x0001u, 16u);
    net_icmp_reset(&state);
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000001u, req, req_len, 1,
                        test_send_ipv4) != 0 ||
        g_send_count != 1 || g_send_proto != NET_L4_PROTO_ICMP ||
        g_send_dst != 0x0A000001u || g_send_len != req_len ||
        g_send_payload[0] != 0u || g_send_payload[8] != req[8]) {
      printf("[icmp] echo reply not built/sent correctly\n");
      fails++;
    }
  }

  /* Oversized echo request (len > 256) -> rejected before the copy. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t req[300];
    net_icmp_reset(&state);
    memset(&stats, 0, sizeof(stats));
    memset(req, 0, sizeof(req));
    req[0] = 8u; /* echo request */
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000001u, req, 257u, 1,
                        test_send_ipv4) != -1 ||
        g_send_count != 0) {
      printf("[icmp] oversized echo request not rejected\n");
      fails++;
    }
  }

  /* Frame shorter than the ICMP header -> rejected before the header read. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t req[8];
    net_icmp_reset(&state);
    memset(&stats, 0, sizeof(stats));
    memset(req, 0, sizeof(req));
    req[0] = 8u;
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000001u, req, 4u, 1,
                        test_send_ipv4) != -1 ||
        g_send_count != 0) {
      printf("[icmp] short ICMP frame not rejected\n");
      fails++;
    }
  }

  /* Echo request not addressed to us (to_local = 0) -> no reply. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t req[32];
    size_t req_len = build_icmp(req, 8u, 1u, 1u, 8u);
    net_icmp_reset(&state);
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000001u, req, req_len, 0,
                        test_send_ipv4) != 0 ||
        g_send_count != 0) {
      printf("[icmp] non-local echo request produced a reply\n");
      fails++;
    }
  }

  /* NULL payload / NULL state -> rejected, nothing sent. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t req[16];
    size_t req_len = build_icmp(req, 8u, 1u, 1u, 8u);
    net_icmp_reset(&state);
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_icmp_handle(&state, &stats, 1u, NULL, req_len, 1, test_send_ipv4) !=
            -1 ||
        net_icmp_handle(NULL, &stats, 1u, req, req_len, 1, test_send_ipv4) !=
            -1 ||
        g_send_count != 0) {
      printf("[icmp] NULL inputs not rejected\n");
      fails++;
    }
  }

  /* Echo reply (type 0) matching an outstanding wait -> recorded, not echoed. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t rep[32];
    size_t rep_len = build_icmp(rep, 0u, 0xABCDu, 0x0007u, 8u);
    net_icmp_reset(&state);
    net_icmp_begin_wait(&state, 0xABCDu, 0x0007u);
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000002u, rep, rep_len, 1,
                        test_send_ipv4) != 0 ||
        state.reply_ready != 1 || state.reply_ip != 0x0A000002u ||
        g_send_count != 0) {
      printf("[icmp] matching echo reply not recorded\n");
      fails++;
    }
  }

  /* Echo reply NOT matching the wait -> reply_ready stays 0. */
  {
    struct net_icmp_state state;
    struct net_stack_stats stats;
    uint8_t rep[32];
    size_t rep_len = build_icmp(rep, 0u, 0x1111u, 0x2222u, 8u);
    net_icmp_reset(&state);
    net_icmp_begin_wait(&state, 0xABCDu, 0x0007u);
    memset(&stats, 0, sizeof(stats));
    reset_capture();
    if (net_icmp_handle(&state, &stats, 0x0A000002u, rep, rep_len, 1,
                        test_send_ipv4) != 0 ||
        state.reply_ready != 0) {
      printf("[icmp] mismatched echo reply wrongly recorded\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] net_icmp OK\n");
  }
  return fails;
}
