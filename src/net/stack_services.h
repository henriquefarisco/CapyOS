#ifndef NET_STACK_SERVICES_H
#define NET_STACK_SERVICES_H

#include "net/stack.h"

#include <stddef.h>
#include <stdint.h>

#define NET_DHCP_CLIENT_PORT 68u
#define NET_DHCP_SERVER_PORT 67u
#define NET_DNS_CLIENT_PORT 53053u
#define NET_DNS_SERVER_PORT 53u
#define NET_DHCP_MSG_DISCOVER 1u
#define NET_DHCP_MSG_OFFER 2u
#define NET_DHCP_MSG_REQUEST 3u
#define NET_DHCP_MSG_ACK 5u
#define NET_DHCP_MSG_NAK 6u

struct net_dhcp_state {
  uint8_t waiting_offer;
  uint8_t waiting_ack;
  uint8_t offer_ready;
  uint8_t ack_ready;
  uint8_t nak_received;
  uint32_t xid;
  uint32_t offered_ip;
  uint32_t ack_ip;
  uint32_t server_id;
  uint32_t subnet_mask;
  uint32_t router;
  uint32_t dns;
};

struct net_dns_state {
  uint8_t waiting_reply;
  uint8_t response_ready;
  uint8_t response_failed;
  uint16_t query_id;
  uint32_t answer_ip;
};

typedef int (*net_service_send_ipv4_fn)(uint8_t protocol, uint32_t dst_ip,
                                        const uint8_t *payload,
                                        size_t payload_len);

void net_dhcp_reset(struct net_dhcp_state *state);
void net_dns_reset(struct net_dns_state *state);
int net_dhcp_handle(struct net_dhcp_state *state, struct net_stack_stats *stats,
                    const struct net_ipv4_config *ipv4,
                    const uint8_t *payload, size_t len);
int net_dns_handle(struct net_dns_state *state, const uint8_t *payload,
                   size_t len);
int net_dhcp_send_message(struct net_dhcp_state *state,
                          const struct net_ipv4_config *ipv4, uint8_t msg_type,
                          uint32_t requested_ip, uint32_t server_id,
                          net_service_send_ipv4_fn send_ipv4);
int net_dns_send_query(struct net_dns_state *state,
                       const struct net_ipv4_config *ipv4,
                       const char *hostname,
                       net_service_send_ipv4_fn send_ipv4);

#endif /* NET_STACK_SERVICES_H */
