#include "../internal/stack_icmp.h"

#include <stddef.h>
#include <stdint.h>

static void icmp_mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void icmp_mem_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static uint16_t icmp_htons16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t icmp_ntohs16(uint16_t v) { return icmp_htons16(v); }

static uint16_t icmp_checksum16(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  while (len >= 2) {
    uint16_t word = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    sum += word;
    data += 2;
    len -= 2;
  }
  if (len) {
    sum += (uint16_t)((uint16_t)data[0] << 8);
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFFu) + (sum >> 16);
  }
  return (uint16_t)(~sum);
}

void net_icmp_reset(struct net_icmp_state *state) {
  if (!state) {
    return;
  }
  icmp_mem_zero(state, sizeof(*state));
}

void net_icmp_begin_wait(struct net_icmp_state *state, uint16_t ident,
                         uint16_t seq) {
  if (!state) {
    return;
  }
  state->waiting = 1;
  state->wait_ident = ident;
  state->wait_seq = seq;
  state->reply_ready = 0;
  state->reply_ip = 0;
}

void net_icmp_end_wait(struct net_icmp_state *state) {
  if (!state) {
    return;
  }
  state->waiting = 0;
  state->reply_ready = 0;
}

int net_icmp_build_echo_request(uint16_t ident, uint16_t seq, uint8_t *out,
                                size_t cap, size_t *out_len) {
  struct net_icmp_hdr *icmp = NULL;
  if (!out || !out_len || cap < 32u) {
    return -1;
  }

  icmp = (struct net_icmp_hdr *)out;
  icmp->type = 8;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->ident = icmp_htons16(ident);
  icmp->sequence = icmp_htons16(seq);
  out[8] = 'h';
  out[9] = 'e';
  out[10] = 'y';
  out[11] = '-';
  out[12] = 'c';
  out[13] = 'a';
  out[14] = 'p';
  out[15] = 'y';
  for (uint32_t i = 16; i < 32u; ++i) {
    out[i] = (uint8_t)i;
  }
  icmp->checksum = icmp_htons16(icmp_checksum16(out, 32u));
  *out_len = 32u;
  return 0;
}

int net_icmp_handle(struct net_icmp_state *state, struct net_stack_stats *stats,
                    uint32_t src_ip, const uint8_t *payload, size_t len,
                    int to_local, net_icmp_send_ipv4_fn send_ipv4) {
  const struct net_icmp_hdr *icmp = NULL;
  if (!state || !stats || !payload) {
    return -1;
  }
  if (len < sizeof(struct net_icmp_hdr)) {
    stats->frames_drop++;
    return -1;
  }

  icmp = (const struct net_icmp_hdr *)payload;
  stats->icmp_rx++;
  if (!to_local) {
    return 0;
  }

  if (icmp->type == 8 && icmp->code == 0) {
    if (!send_ipv4 || len > 256u) {
      return -1;
    }
    uint8_t reply[256];
    struct net_icmp_hdr *hdr = NULL;
    icmp_mem_copy(reply, payload, len);
    hdr = (struct net_icmp_hdr *)reply;
    hdr->type = 0;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->checksum = icmp_htons16(icmp_checksum16(reply, len));
    return send_ipv4(NET_L4_PROTO_ICMP, src_ip, reply, len);
  }

  if (icmp->type == 0 && icmp->code == 0 && state->waiting) {
    uint16_t ident = icmp_ntohs16(icmp->ident);
    uint16_t seq = icmp_ntohs16(icmp->sequence);
    if (ident == state->wait_ident && seq == state->wait_seq) {
      state->reply_ready = 1;
      state->reply_ip = src_ip;
    }
  }
  return 0;
}
