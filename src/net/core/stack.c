#include "net/stack.h"

#include "kernel/log/klog.h"
#include "net/dns.h"
#include "net/hyperv_runtime.h"
#include "net/hyperv_runtime_gate.h"
#include "net/hyperv_runtime_policy.h"
#include "../internal/stack_arp.h"
#include "../internal/stack_driver.h"
#include "../internal/stack_icmp.h"
#include "../internal/stack_ipv4.h"
#include "../internal/stack_services.h"
#include "../internal/stack_selftest.h"
#include "../internal/stack_utils.h"

#include "core/system_init.h"

#include <stddef.h>
#include <stdint.h>

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_FRAME_MAX 1600u
#define NET_POLL_BUDGET 16u

struct net_eth_hdr {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t ethertype;
} __attribute__((packed));

struct net_state {
  uint8_t initialized;
  uint8_t ready;
  uint8_t driver_available;
  uint8_t dhcp_lease_acquired;
  uint16_t ident_seq;
  uint32_t age_ticks;
  uint32_t dhcp_attempts;
  int32_t dhcp_last_error;
  const char *unavailable_reason;
  struct net_nic_probe nic;
  struct net_ipv4_config ipv4;
  struct net_stack_stats stats;
  struct net_arp_entry arp[NET_ARP_CAPACITY];
  struct net_dhcp_state dhcp;
  struct net_dns_state dns;
  struct net_icmp_state icmp;
  struct net_hyperv_runtime_state hyperv_runtime;
};

static struct net_state g_net;

static int ip_is_broadcast(uint32_t ip) {
  return ip == NET_IPV4_ADDR(255, 255, 255, 255);
}

static void set_default_config(void) {
  g_net.ipv4.addr = NET_IPV4_ADDR(10, 0, 2, 15);
  g_net.ipv4.mask = NET_IPV4_ADDR(255, 255, 255, 0);
  g_net.ipv4.gateway = NET_IPV4_ADDR(10, 0, 2, 2);
  g_net.ipv4.dns = NET_IPV4_ADDR(1, 1, 1, 1);
  g_net.ipv4.mtu = 1500;
  g_net.ipv4.ttl = 64;
  g_net.ipv4.mac[0] = 0x02u;
  g_net.ipv4.mac[1] = 0xCAu;
  g_net.ipv4.mac[2] = 0x50u;
  g_net.ipv4.mac[3] = 0x59u;
  g_net.ipv4.mac[4] = 0x00u;
  g_net.ipv4.mac[5] = 0x64u;
}

static int arp_send_frame_cb(const uint8_t *frame, uint16_t len) {
  return net_stack_driver_send_frame(&g_net.nic, frame, len);
}

static int arp_poll_cb(void) { return net_stack_poll(); }

static void arp_delay_cb(void) { net_stack_delay_approx_1ms(); }

static struct net_stack_ipv4_runtime stack_ipv4_runtime(void) {
  struct net_stack_ipv4_runtime runtime;

  runtime.initialized = g_net.initialized;
  runtime.ready = g_net.ready;
  runtime.ident_seq = &g_net.ident_seq;
  runtime.age_ticks = &g_net.age_ticks;
  runtime.ipv4 = &g_net.ipv4;
  runtime.stats = &g_net.stats;
  runtime.arp_entries = g_net.arp;
  runtime.arp_capacity = NET_ARP_CAPACITY;
  runtime.dhcp = &g_net.dhcp;
  runtime.dns = &g_net.dns;
  runtime.icmp = &g_net.icmp;
  return runtime;
}

int net_stack_init(void) {
  net_stack_mem_zero(&g_net, sizeof(g_net));
  set_default_config();
  g_net.ident_seq = 1;
  g_net.driver_available = 0;
  g_net.unavailable_reason = NULL;
  net_hyperv_runtime_state_init(&g_net.hyperv_runtime);

  klog(KLOG_INFO, "[net] net_stack_init: probing NICs...");

  if (net_probe_first_supported(&g_net.nic) == 0 && g_net.nic.found) {
    g_net.driver_available = 1;
    g_net.ipv4.mtu = g_net.nic.mtu;
    net_stack_mem_copy(g_net.ipv4.mac, g_net.nic.mac, 6);
    g_net.ready = 0;

    klog_hex(KLOG_INFO, "[net] NIC found kind=", (uint64_t)g_net.nic.kind);
    klog_hex(KLOG_INFO, "[net] NIC vendor=", (uint64_t)g_net.nic.vendor_id);
    klog_hex(KLOG_INFO, "[net] NIC device=", (uint64_t)g_net.nic.device_id);

    if (net_stack_driver_init_runtime(&g_net.nic, g_net.ipv4.mac) == 0) {
      g_net.ready =
          (uint8_t)(g_net.nic.kind != NET_NIC_KIND_HYPERV_NETVSC ? 1 : 0);
      if (g_net.ready) {
        klog(KLOG_INFO, "[net] Driver runtime ready (immediate).");
      } else {
        klog(KLOG_INFO, "[net] Driver runtime deferred (NetVSC path).");
      }
    } else {
      klog(KLOG_WARN, "[net] Driver runtime init failed; stack not ready.");
      g_net.unavailable_reason = "driver runtime init failed";
    }

    if (g_net.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
      klog(KLOG_INFO, "[net] Configuring Hyper-V NetVSC runtime...");
      if (net_hyperv_runtime_state_configure(&g_net.nic,
                                              &g_net.hyperv_runtime) != 0) {
        klog(KLOG_WARN, "[net] Hyper-V runtime configure returned error.");
      }
      net_hyperv_runtime_state_apply_nic(&g_net.hyperv_runtime, &g_net.nic);
    }
  } else {
    g_net.ready = 0;
    g_net.driver_available = 0;
    g_net.unavailable_reason = "no compatible NIC found";
    klog(KLOG_WARN, "[net] No compatible NIC found. Continuing without network.");
  }

  g_net.initialized = 1;

  /* Always return 0 so the boot flow continues even without network.
   * Callers check net_stack_ready() or net_stack_driver_available()
   * to determine actual network status. */
  if (!g_net.ready && !g_net.driver_available) {
    klog(KLOG_WARN, "[net] AVISO: Nenhum driver de rede compativel. Sistema continuara sem rede.");
  }
  return 0;
}

int net_stack_ready(void) { return g_net.initialized && g_net.ready; }

int net_stack_refresh_runtime(void) {
  struct system_runtime_platform platform;
  int rc = 0;

  if (!g_net.initialized || !g_net.nic.found) {
    return -1;
  }
  if (g_net.ready) {
    return 0;
  }
  if (g_net.nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return -2;
  }
  system_runtime_platform_get(&platform);
  switch (net_hyperv_runtime_gate_state_for(
      g_net.nic.found, g_net.nic.kind, g_net.hyperv_runtime.runtime_configured,
      g_net.hyperv_runtime.bus_connected, &platform)) {
  case NET_HYPERV_RUNTIME_GATE_WAIT_PLATFORM:
  case NET_HYPERV_RUNTIME_GATE_WAIT_STORAGE:
  case NET_HYPERV_RUNTIME_GATE_WAIT_BUS:
    return 0;
  case NET_HYPERV_RUNTIME_GATE_WAIT_RUNTIME:
    return -3;
  default:
    break;
  }
  rc = net_hyperv_runtime_state_refresh(g_net.initialized, g_net.ready,
                                        &g_net.nic, &g_net.hyperv_runtime);
  net_hyperv_runtime_state_apply_nic(&g_net.hyperv_runtime, &g_net.nic);

  /* Late promotion: once the NetVSC backend has completed its VMBus channel
   * open and NetVSP/RNDIS control handshake, promote the stack to ready so
   * that hey, net-mode dhcp, and other commands can proceed. */
  if (!g_net.ready &&
      g_net.hyperv_runtime.runtime_phase == NETVSC_RUNTIME_READY) {
    g_net.ready = 1;
    /* Propagate real MAC and MTU from the NetVSC control handshake. */
    if (g_net.hyperv_runtime.controller.backend.session.control.mac_valid) {
      net_stack_mem_copy(
          g_net.ipv4.mac,
          g_net.hyperv_runtime.controller.backend.session.control.mac, 6);
    }
    if (g_net.hyperv_runtime.controller.backend.session.control.mtu != 0u) {
      g_net.ipv4.mtu =
          (uint16_t)g_net.hyperv_runtime.controller.backend.session.control.mtu;
      g_net.nic.mtu = g_net.ipv4.mtu;
    }
  }
  return rc;
}

int net_stack_set_ipv4(uint32_t addr, uint32_t mask, uint32_t gateway,
                       uint32_t dns) {
  if (!g_net.initialized) {
    return -1;
  }
  g_net.ipv4.addr = addr;
  g_net.ipv4.mask = mask;
  g_net.ipv4.gateway = gateway;
  g_net.ipv4.dns = dns;
  net_stack_mem_zero(g_net.arp, sizeof(g_net.arp));
  net_dhcp_reset(&g_net.dhcp);
  g_net.dhcp_lease_acquired = 0;
  net_dns_reset(&g_net.dns);
  net_icmp_reset(&g_net.icmp);
  return 0;
}

int net_stack_status(struct net_stack_status *out) {
  struct system_runtime_platform platform;

  if (!out || !g_net.initialized) {
    return -1;
  }
  out->initialized = g_net.initialized;
  out->ready = g_net.ready;
  out->runtime_supported = g_net.nic.runtime_supported;
  out->hyperv_vmbus_stage = g_net.hyperv_runtime.vmbus_stage;
  out->hyperv_stage = g_net.hyperv_runtime.stage;
  out->hyperv_offer_ready = g_net.hyperv_runtime.offer_ready;
  out->hyperv_bus_prepared = g_net.hyperv_runtime.bus_prepared;
  out->hyperv_channel_ready = g_net.hyperv_runtime.channel_ready;
  out->hyperv_bus_connected = g_net.hyperv_runtime.bus_connected;
  out->hyperv_runtime_configured = g_net.hyperv_runtime.runtime_configured;
  out->hyperv_runtime_enabled = g_net.hyperv_runtime.runtime_enabled;
  out->hyperv_runtime_phase = g_net.hyperv_runtime.runtime_phase;
  out->hyperv_refresh_action = NET_HYPERV_RUNTIME_ACTION_INVALID_KIND;
  out->hyperv_gate_state = NET_HYPERV_RUNTIME_GATE_INVALID;
  out->dhcp_lease_acquired = g_net.dhcp_lease_acquired;
  out->arp_entries = net_arp_count(g_net.arp, NET_ARP_CAPACITY);
  out->dhcp_attempts = g_net.dhcp_attempts;
  out->hyperv_refresh_attempts = g_net.hyperv_runtime.refresh_attempts;
  out->hyperv_refresh_changes = g_net.hyperv_runtime.refresh_changes;
  out->dhcp_last_error = g_net.dhcp_last_error;
  out->hyperv_last_error = g_net.hyperv_runtime.last_error;
  out->hyperv_last_result = g_net.hyperv_runtime.last_result;
  out->nic = g_net.nic;
  out->ipv4 = g_net.ipv4;
  out->stats = g_net.stats;
  if (g_net.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
    struct net_hyperv_runtime_policy policy;
    struct net_hyperv_runtime_snapshot snapshot;

    system_runtime_platform_get(&platform);
    net_hyperv_runtime_snapshot(&g_net.nic, &g_net.hyperv_runtime, &snapshot);
    net_hyperv_runtime_policy_plan(g_net.initialized, g_net.ready, &g_net.nic,
                                   &snapshot, &policy);
    out->hyperv_refresh_action = policy.action;
    out->hyperv_gate_state = net_hyperv_runtime_gate_state_for(
        g_net.nic.found, g_net.nic.kind,
        g_net.hyperv_runtime.runtime_configured,
        g_net.hyperv_runtime.bus_connected, &platform);
  }
  return 0;
}

int net_stack_receive_frame(const uint8_t *frame, size_t len) {
  if (!g_net.initialized || !frame || len < sizeof(struct net_eth_hdr)) {
    return -1;
  }

  const struct net_eth_hdr *eth = (const struct net_eth_hdr *)frame;
  uint16_t ethertype = net_stack_ntohs16(eth->ethertype);
  const uint8_t *payload = frame + sizeof(struct net_eth_hdr);
  size_t payload_len = len - sizeof(struct net_eth_hdr);

  g_net.stats.frames_rx++;
  switch (ethertype) {
  case NET_ETHERTYPE_ARP:
    return net_arp_handle(g_net.arp, NET_ARP_CAPACITY, &g_net.age_ticks,
                          &g_net.stats, &g_net.ipv4, payload, payload_len,
                          g_net.ready, arp_send_frame_cb);
  case NET_ETHERTYPE_IPV4:
    {
      struct net_stack_ipv4_runtime runtime = stack_ipv4_runtime();
      return net_stack_ipv4_handle(&runtime, payload, payload_len, eth->src,
                                   net_stack_send_ipv4);
    }
  default:
    g_net.stats.eth_unknown++;
    g_net.stats.frames_drop++;
    return -1;
  }
}

int net_stack_poll(void) {
  if (!g_net.initialized || !g_net.ready) {
    return 0;
  }

  uint8_t frame[NET_FRAME_MAX];
  uint16_t frame_len = 0;
  int processed = 0;
  for (uint32_t i = 0; i < NET_POLL_BUDGET; ++i) {
    int rc = net_stack_driver_poll_frame(&g_net.nic, frame, sizeof(frame),
                                         &frame_len);
    if (rc <= 0) {
      break;
    }
    (void)net_stack_receive_frame(frame, frame_len);
    processed++;
  }
  return processed;
}

int net_stack_send_ipv4(uint8_t protocol, uint32_t dst_ip,
                        const uint8_t *payload, size_t payload_len) {
  static const uint8_t broadcast_mac[6] = {0xFFu, 0xFFu, 0xFFu,
                                           0xFFu, 0xFFu, 0xFFu};
  if (!g_net.initialized || !g_net.ready || !payload || payload_len == 0) {
    g_net.stats.frames_drop++;
    return -1;
  }

  size_t ip_payload_max =
      (g_net.ipv4.mtu > NET_IPV4_HEADER_SIZE)
          ? (g_net.ipv4.mtu - NET_IPV4_HEADER_SIZE)
          : 0;
  if (payload_len > ip_payload_max) {
    g_net.stats.frames_drop++;
    return -1;
  }

  if (ip_is_broadcast(dst_ip)) {
    struct net_stack_ipv4_runtime runtime = stack_ipv4_runtime();
    return net_stack_ipv4_send_frame(&runtime, protocol, g_net.ipv4.addr,
                                     dst_ip, broadcast_mac, payload,
                                     payload_len, arp_send_frame_cb);
  }

  uint32_t next_hop =
      net_arp_route_next_hop(g_net.ipv4.addr, g_net.ipv4.mask,
                             g_net.ipv4.gateway, dst_ip);
  if (net_arp_resolve(g_net.arp, NET_ARP_CAPACITY, &g_net.age_ticks,
                      &g_net.stats, &g_net.ipv4, next_hop, arp_send_frame_cb,
                      arp_poll_cb, arp_delay_cb) != 0) {
    g_net.stats.frames_drop++;
    return -1;
  }
  int arp_idx = net_arp_find(g_net.arp, NET_ARP_CAPACITY, next_hop);
  if (arp_idx < 0) {
    g_net.stats.frames_drop++;
    return -1;
  }
  g_net.stats.arp_hits++;
  {
    struct net_stack_ipv4_runtime runtime = stack_ipv4_runtime();
    return net_stack_ipv4_send_frame(&runtime, protocol, g_net.ipv4.addr,
                                     dst_ip, g_net.arp[arp_idx].mac, payload,
                                     payload_len, arp_send_frame_cb);
  }
}

int net_stack_ping(uint32_t dst_ip, uint32_t timeout_ms, uint32_t *rtt_ms,
                   uint32_t *reply_ip) {
  if (!g_net.initialized || !g_net.ready || dst_ip == 0 || timeout_ms == 0) {
    return -1;
  }

  uint32_t next_hop =
      net_arp_route_next_hop(g_net.ipv4.addr, g_net.ipv4.mask,
                             g_net.ipv4.gateway, dst_ip);
  if (net_arp_resolve(g_net.arp, NET_ARP_CAPACITY, &g_net.age_ticks,
                      &g_net.stats, &g_net.ipv4, next_hop, arp_send_frame_cb,
                      arp_poll_cb, arp_delay_cb) != 0) {
    return -1;
  }

  uint8_t icmp_pkt[32];
  uint16_t seq = g_net.ident_seq++;
  uint16_t ident = 0xCA50u;
  size_t icmp_len = 0;

  if (net_icmp_build_echo_request(ident, seq, icmp_pkt, sizeof(icmp_pkt),
                                  &icmp_len) != 0) {
    return -1;
  }
  net_icmp_begin_wait(&g_net.icmp, ident, seq);

  if (net_stack_send_ipv4(NET_L4_PROTO_ICMP, dst_ip, icmp_pkt,
                          icmp_len) != 0) {
    net_icmp_end_wait(&g_net.icmp);
    return -1;
  }

  for (uint32_t elapsed = 0; elapsed <= timeout_ms; ++elapsed) {
    (void)net_stack_poll();
    if (g_net.icmp.reply_ready) {
      g_net.icmp.waiting = 0;
      if (rtt_ms) {
        *rtt_ms = elapsed;
      }
      if (reply_ip) {
        *reply_ip = g_net.icmp.reply_ip;
      }
      g_net.icmp.reply_ready = 0;
      return 0;
    }
    net_stack_delay_approx_1ms();
  }

  net_icmp_end_wait(&g_net.icmp);
  return -1;
}

int net_stack_dhcp_acquire(uint32_t timeout_ms) {
  uint32_t fallback_addr = 0;
  uint32_t fallback_mask = 0;
  uint32_t fallback_gateway = 0;
  uint32_t fallback_dns = 0;
  uint32_t stage_timeout = 0;

  g_net.dhcp_attempts++;
  if (!g_net.initialized || !g_net.ready || timeout_ms < 200u) {
    g_net.dhcp_last_error = -1;
    return -1;
  }
  g_net.dhcp_lease_acquired = 0;

  fallback_addr = g_net.ipv4.addr;
  fallback_mask = g_net.ipv4.mask;
  fallback_gateway = g_net.ipv4.gateway;
  fallback_dns = g_net.ipv4.dns;
  stage_timeout = timeout_ms / 2u;
  if (stage_timeout < 150u) {
    stage_timeout = 150u;
  }

  net_dhcp_reset(&g_net.dhcp);
  g_net.dhcp.xid =
      (0xCA500000u ^ ((uint32_t)g_net.ident_seq << 8) ^ g_net.age_ticks);
  g_net.dhcp.waiting_offer = 1;

  if (net_dhcp_send_message(&g_net.dhcp, &g_net.ipv4,
                            NET_DHCP_MSG_DISCOVER, 0u, 0u,
                            net_stack_send_ipv4) != 0) {
    net_dhcp_reset(&g_net.dhcp);
    g_net.dhcp_last_error = -2;
    return -1;
  }

  for (uint32_t elapsed = 0; elapsed <= stage_timeout; ++elapsed) {
    (void)net_stack_poll();
    if (g_net.dhcp.offer_ready) {
      break;
    }
    net_stack_delay_approx_1ms();
  }
  if (!g_net.dhcp.offer_ready || g_net.dhcp.offered_ip == 0u ||
      g_net.dhcp.server_id == 0u) {
    net_dhcp_reset(&g_net.dhcp);
    g_net.dhcp_last_error = -3;
    return -1;
  }

  g_net.dhcp.waiting_offer = 0;
  g_net.dhcp.waiting_ack = 1;
  g_net.dhcp.ack_ready = 0;
  g_net.dhcp.nak_received = 0;
  if (net_dhcp_send_message(&g_net.dhcp, &g_net.ipv4,
                            NET_DHCP_MSG_REQUEST,
                            g_net.dhcp.offered_ip, g_net.dhcp.server_id,
                            net_stack_send_ipv4) != 0) {
    net_dhcp_reset(&g_net.dhcp);
    g_net.dhcp_last_error = -4;
    return -1;
  }

  for (uint32_t elapsed = 0; elapsed <= stage_timeout; ++elapsed) {
    (void)net_stack_poll();
    if (g_net.dhcp.ack_ready || g_net.dhcp.nak_received) {
      break;
    }
    net_stack_delay_approx_1ms();
  }
  if (!g_net.dhcp.ack_ready || g_net.dhcp.nak_received ||
      g_net.dhcp.ack_ip == 0u) {
    net_dhcp_reset(&g_net.dhcp);
    g_net.dhcp_last_error = -5;
    return -1;
  }

  if (g_net.dhcp.subnet_mask == 0u) {
    g_net.dhcp.subnet_mask = fallback_mask;
  }
  if (g_net.dhcp.router == 0u) {
    g_net.dhcp.router = fallback_gateway;
  }
  if (g_net.dhcp.dns == 0u) {
    g_net.dhcp.dns = fallback_dns;
  }

  (void)net_stack_set_ipv4(g_net.dhcp.ack_ip,
                           g_net.dhcp.subnet_mask ? g_net.dhcp.subnet_mask
                                                  : fallback_mask,
                           g_net.dhcp.router, g_net.dhcp.dns);
  if (g_net.ipv4.addr == 0u) {
    g_net.ipv4.addr = fallback_addr;
    g_net.dhcp_last_error = -6;
    return -1;
  }
  g_net.dhcp_lease_acquired = 1;
  g_net.dhcp_last_error = 0;
  return 0;
}

int net_stack_dns_resolve(const char *hostname, uint32_t timeout_ms,
                          uint32_t *out_ip) {
  if (!g_net.initialized || !g_net.ready || !hostname || !out_ip ||
      timeout_ms < 100u || g_net.ipv4.addr == 0u || g_net.ipv4.dns == 0u) {
    return -1;
  }

  net_dns_reset(&g_net.dns);
  g_net.dns.waiting_reply = 1;
  g_net.dns.query_id =
      (uint16_t)(0xCA00u ^ g_net.ident_seq ^ (uint16_t)g_net.age_ticks);
  if (g_net.dns.query_id == 0u) {
    g_net.dns.query_id = 0xCA51u;
  }

  if (net_dns_send_query(&g_net.dns, &g_net.ipv4, hostname,
                         net_stack_send_ipv4) != 0) {
    net_dns_reset(&g_net.dns);
    return -1;
  }

  for (uint32_t elapsed = 0; elapsed <= timeout_ms; ++elapsed) {
    (void)net_stack_poll();
    if (g_net.dns.response_ready) {
      *out_ip = g_net.dns.answer_ip;
      net_dns_reset(&g_net.dns);
      return 0;
    }
    if (g_net.dns.response_failed) {
      break;
    }
    net_stack_delay_approx_1ms();
  }

  net_dns_reset(&g_net.dns);
  return -1;
}

int net_stack_driver_available(void) {
  return g_net.initialized && g_net.driver_available;
}

const char *net_stack_unavailable_reason(void) {
  if (!g_net.initialized) {
    return "stack not initialized";
  }
  if (g_net.driver_available && g_net.ready) {
    return NULL; /* available */
  }
  return g_net.unavailable_reason ? g_net.unavailable_reason : "unknown";
}

int net_stack_protocol_selftest(void) {
  if (!g_net.initialized) {
    return -1;
  }
  return net_stack_protocol_selftest_run(&g_net.ipv4, &g_net.stats,
                                         net_stack_receive_frame);
}

const char *net_driver_name(uint8_t kind) { return net_probe_kind_name(kind); }
