#include "net/internal/stack_services.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_MAX 512u
#define UDP_HEADER_SIZE 8u
#define DHCP_MIN_MESSAGE_SIZE 300u
#define BOOTP_FLAGS_OFFSET (UDP_HEADER_SIZE + 10u)
#define BOOTP_CIADDR_OFFSET (UDP_HEADER_SIZE + 12u)
#define BOOTP_CHADDR_OFFSET (UDP_HEADER_SIZE + 28u)
#define BOOTP_MAGIC_OFFSET (UDP_HEADER_SIZE + 236u)
#define DHCP_OPTIONS_OFFSET (UDP_HEADER_SIZE + 240u)

static uint8_t g_protocol;
static uint32_t g_src_ip;
static uint32_t g_dst_ip;
static uint8_t g_payload[CAPTURE_MAX];
static size_t g_payload_len;

static int capture_ipv4(uint8_t protocol, uint32_t src_ip, uint32_t dst_ip,
                        const uint8_t *payload, size_t payload_len) {
  g_protocol = protocol;
  g_src_ip = src_ip;
  g_dst_ip = dst_ip;
  g_payload_len = payload_len;
  if (!payload || payload_len > sizeof(g_payload)) {
    return -1;
  }
  memcpy(g_payload, payload, payload_len);
  return 0;
}

static void reset_capture(void) {
  g_protocol = 0u;
  g_src_ip = 0xFFFFFFFFu;
  g_dst_ip = 0u;
  g_payload_len = 0u;
  memset(g_payload, 0, sizeof(g_payload));
}

static int expect(int condition, const char *message) {
  if (condition) {
    return 0;
  }
  printf("[dhcp-wire] %s\n", message);
  return 1;
}

int run_net_dhcp_wire_tests(void) {
  struct net_dhcp_state state;
  struct net_dns_state dns;
  struct net_ipv4_config ipv4;
  static const uint8_t expected_mac[6] = {0x00u, 0x50u, 0x56u,
                                          0x12u, 0x34u, 0x56u};
  int failures = 0;

  memset(&state, 0, sizeof(state));
  memset(&dns, 0, sizeof(dns));
  memset(&ipv4, 0, sizeof(ipv4));
  state.xid = 0x12345678u;
  ipv4.addr = NET_IPV4_ADDR(10, 0, 2, 15);
  ipv4.dns = NET_IPV4_ADDR(1, 1, 1, 1);
  memcpy(ipv4.mac, expected_mac, sizeof(expected_mac));

  reset_capture();
  failures += expect(
      net_dhcp_send_message(&state, &ipv4, NET_DHCP_MSG_DISCOVER, 0u, 0u,
                            capture_ipv4) == 0,
      "DHCPDISCOVER construction failed");
  failures += expect(g_protocol == NET_L4_PROTO_UDP,
                     "DHCPDISCOVER must use UDP");
  failures += expect(g_src_ip == 0u,
                     "DHCPDISCOVER must use IPv4 source 0.0.0.0");
  failures += expect(g_dst_ip == NET_IPV4_ADDR(255, 255, 255, 255),
                     "DHCPDISCOVER must use the limited broadcast");
  failures += expect(g_payload_len > BOOTP_MAGIC_OFFSET + 4u,
                     "DHCPDISCOVER wire payload is truncated");
  failures += expect(g_payload_len == UDP_HEADER_SIZE + DHCP_MIN_MESSAGE_SIZE,
                     "DHCPDISCOVER must carry the 300-byte BOOTP minimum");
  failures += expect(g_payload[4] == 0x01u && g_payload[5] == 0x34u,
                     "DHCPDISCOVER UDP length must include BOOTP padding");
  failures += expect(g_payload[DHCP_OPTIONS_OFFSET + 8u] == 0xFFu,
                     "DHCPDISCOVER END option must precede BOOTP padding");
  failures += expect(g_payload[g_payload_len - 1u] == 0u,
                     "DHCPDISCOVER bytes after END must be PAD options");
  failures += expect(g_payload[0] == 0u && g_payload[1] == 68u &&
                         g_payload[2] == 0u && g_payload[3] == 67u,
                     "DHCP client/server UDP ports are wrong");
  failures += expect(g_payload[BOOTP_FLAGS_OFFSET] == 0x80u &&
                         g_payload[BOOTP_FLAGS_OFFSET + 1u] == 0u,
                     "DHCP broadcast flag is missing");
  failures += expect(g_payload[BOOTP_CIADDR_OFFSET] == 0u &&
                         g_payload[BOOTP_CIADDR_OFFSET + 1u] == 0u &&
                         g_payload[BOOTP_CIADDR_OFFSET + 2u] == 0u &&
                         g_payload[BOOTP_CIADDR_OFFSET + 3u] == 0u,
                     "DHCP ciaddr must remain zero in INIT");
  failures += expect(
      memcmp(&g_payload[BOOTP_CHADDR_OFFSET], expected_mac,
             sizeof(expected_mac)) == 0,
      "DHCP chaddr does not match the NIC MAC");
  failures += expect(g_payload[BOOTP_MAGIC_OFFSET] == 0x63u &&
                         g_payload[BOOTP_MAGIC_OFFSET + 1u] == 0x82u &&
                         g_payload[BOOTP_MAGIC_OFFSET + 2u] == 0x53u &&
                         g_payload[BOOTP_MAGIC_OFFSET + 3u] == 0x63u,
                     "DHCP magic cookie is wrong");

  reset_capture();
  failures += expect(
      net_dhcp_send_message(
          &state, &ipv4, NET_DHCP_MSG_REQUEST,
          NET_IPV4_ADDR(192, 168, 87, 128), NET_IPV4_ADDR(192, 168, 87, 254),
          capture_ipv4) == 0,
      "DHCPREQUEST construction failed");
  failures += expect(g_src_ip == 0u,
                     "DHCPREQUEST in SELECTING must use source 0.0.0.0");
  failures += expect(g_dst_ip == NET_IPV4_ADDR(255, 255, 255, 255),
                     "DHCPREQUEST in SELECTING must stay broadcast");
  failures += expect(g_payload_len == UDP_HEADER_SIZE + DHCP_MIN_MESSAGE_SIZE,
                     "DHCPREQUEST must carry the 300-byte BOOTP minimum");
  failures += expect(g_payload[DHCP_OPTIONS_OFFSET + 20u] == 0xFFu,
                     "DHCPREQUEST END option must precede BOOTP padding");
  failures += expect(g_payload[g_payload_len - 1u] == 0u,
                     "DHCPREQUEST bytes after END must be PAD options");

  dns.query_id = 0xCA51u;
  reset_capture();
  failures += expect(
      net_dns_send_query(&dns, &ipv4, "example.test", capture_ipv4) == 0,
      "DNS construction failed");
  failures += expect(g_src_ip == ipv4.addr,
                     "ordinary DNS traffic must retain the configured source");
  failures += expect(g_dst_ip == ipv4.dns,
                     "ordinary DNS traffic must retain the configured target");

  if (failures == 0) {
    printf("[tests] net_dhcp_wire OK\n");
  }
  return failures;
}
