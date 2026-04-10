#include "net/tcp.h"
#include "net/stack.h"
#include "security/csprng.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct tcp_connection tcp_conns[TCP_MAX_CONNECTIONS];
static struct tcp_stats tcp_global_stats;

static void tcp_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void tcp_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

static uint16_t tcp_htons(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t tcp_htonl(uint32_t v) {
  return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
         ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}

static uint32_t tcp_ntohl(uint32_t v) { return tcp_htonl(v); }
static uint16_t tcp_ntohs(uint16_t v) { return tcp_htons(v); }

static uint32_t tcp_gen_isn(void) {
  uint8_t buf[4];
  csprng_fill(buf, 4);
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | buf[3];
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t *tcp_data, size_t tcp_len) {
  uint32_t sum = 0;
  sum += (src_ip >> 16) & 0xFFFF;
  sum += src_ip & 0xFFFF;
  sum += (dst_ip >> 16) & 0xFFFF;
  sum += dst_ip & 0xFFFF;
  sum += tcp_htons(6);
  sum += tcp_htons((uint16_t)tcp_len);
  for (size_t i = 0; i + 1 < tcp_len; i += 2) {
    sum += ((uint32_t)tcp_data[i] << 8) | tcp_data[i + 1];
  }
  if (tcp_len & 1) sum += (uint32_t)tcp_data[tcp_len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return (uint16_t)~sum;
}

static int tcp_send_segment(struct tcp_connection *c, uint8_t flags,
                             const void *data, size_t len) {
  uint8_t pkt[20 + TCP_WINDOW_SIZE];
  size_t hdr_len = 20;
  if (flags & TCP_FLAG_SYN) hdr_len = 24;

  struct tcp_header *hdr = (struct tcp_header *)pkt;
  hdr->src_port = tcp_htons(c->local_port);
  hdr->dst_port = tcp_htons(c->remote_port);
  hdr->seq_num = tcp_htonl(c->snd_nxt);
  hdr->ack_num = tcp_htonl((flags & TCP_FLAG_ACK) ? c->rcv_nxt : 0);
  hdr->data_offset = (uint8_t)((hdr_len / 4) << 4);
  hdr->flags = flags;
  hdr->window = tcp_htons((uint16_t)c->rcv_wnd);
  hdr->checksum = 0;
  hdr->urgent = 0;

  if (flags & TCP_FLAG_SYN) {
    pkt[20] = 2;
    pkt[21] = 4;
    pkt[22] = (uint8_t)(c->mss >> 8);
    pkt[23] = (uint8_t)c->mss;
  }

  if (data && len > 0) {
    tcp_memcpy(pkt + hdr_len, data, len);
  }

  size_t total = hdr_len + len;
  hdr->checksum = tcp_htons(tcp_checksum(c->local_ip, c->remote_ip, pkt, total));

  int r = net_stack_send_ipv4(NET_L4_PROTO_TCP, c->remote_ip, pkt, total);
  if (r == 0) {
    tcp_global_stats.segments_sent++;
    if (flags & TCP_FLAG_SYN) c->snd_nxt++;
    else if (flags & TCP_FLAG_FIN) c->snd_nxt++;
    else c->snd_nxt += (uint32_t)len;
  }
  return r;
}

void tcp_init(void) {
  tcp_memset(tcp_conns, 0, sizeof(tcp_conns));
  tcp_memset(&tcp_global_stats, 0, sizeof(tcp_global_stats));
}

int tcp_open(uint32_t local_ip, uint16_t local_port,
             uint32_t remote_ip, uint16_t remote_port, int socket_fd) {
  for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
    if (!tcp_conns[i].active) {
      struct tcp_connection *c = &tcp_conns[i];
      tcp_memset(c, 0, sizeof(*c));
      c->active = 1;
      c->local_ip = local_ip;
      c->local_port = local_port;
      c->remote_ip = remote_ip;
      c->remote_port = remote_port;
      c->socket_fd = socket_fd;
      c->state = TCP_STATE_SYN_SENT;
      c->iss = tcp_gen_isn();
      c->snd_una = c->iss;
      c->snd_nxt = c->iss;
      c->snd_wnd = TCP_WINDOW_SIZE;
      c->rcv_wnd = TCP_WINDOW_SIZE;
      c->mss = TCP_MSS_DEFAULT;
      c->rto_ms = TCP_RTO_INIT_MS;
      c->send_buf = (uint8_t *)kmalloc(TCP_WINDOW_SIZE);
      c->recv_buf = (uint8_t *)kmalloc(TCP_WINDOW_SIZE);
      c->send_cap = c->send_buf ? TCP_WINDOW_SIZE : 0;
      c->recv_cap = c->recv_buf ? TCP_WINDOW_SIZE : 0;

      tcp_send_segment(c, TCP_FLAG_SYN, NULL, 0);
      return i;
    }
  }
  return -1;
}

int tcp_listen(uint32_t local_ip, uint16_t local_port, int socket_fd) {
  for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
    if (!tcp_conns[i].active) {
      struct tcp_connection *c = &tcp_conns[i];
      tcp_memset(c, 0, sizeof(*c));
      c->active = 1;
      c->local_ip = local_ip;
      c->local_port = local_port;
      c->socket_fd = socket_fd;
      c->state = TCP_STATE_LISTEN;
      c->rcv_wnd = TCP_WINDOW_SIZE;
      c->mss = TCP_MSS_DEFAULT;
      c->send_buf = (uint8_t *)kmalloc(TCP_WINDOW_SIZE);
      c->recv_buf = (uint8_t *)kmalloc(TCP_WINDOW_SIZE);
      c->send_cap = c->send_buf ? TCP_WINDOW_SIZE : 0;
      c->recv_cap = c->recv_buf ? TCP_WINDOW_SIZE : 0;
      return i;
    }
  }
  return -1;
}

int tcp_send(int conn_id, const void *data, size_t len) {
  if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
  struct tcp_connection *c = &tcp_conns[conn_id];
  if (!c->active || c->state != TCP_STATE_ESTABLISHED) return -1;
  if (!c->send_buf) return -1;

  size_t sent = 0;
  while (sent < len) {
    size_t chunk = len - sent;
    if (chunk > c->mss) chunk = c->mss;
    int r = tcp_send_segment(c, TCP_FLAG_ACK | TCP_FLAG_PSH,
                              (const uint8_t *)data + sent, chunk);
    if (r != 0) return sent > 0 ? (int)sent : -1;
    sent += chunk;
  }
  return (int)sent;
}

int tcp_recv(int conn_id, void *buf, size_t len) {
  if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
  struct tcp_connection *c = &tcp_conns[conn_id];
  if (!c->active) return -1;
  if (c->recv_len == 0 || !c->recv_buf) {
    if (c->state == TCP_STATE_CLOSE_WAIT || c->state == TCP_STATE_CLOSED)
      return 0;
    return -1;
  }
  uint32_t avail = c->recv_len;
  if (avail > len) avail = (uint32_t)len;
  tcp_memcpy(buf, c->recv_buf + c->recv_head, avail);
  c->recv_head += avail;
  c->recv_len -= avail;
  if (c->recv_len == 0) c->recv_head = 0;
  return (int)avail;
}

int tcp_close(int conn_id) {
  if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
  struct tcp_connection *c = &tcp_conns[conn_id];
  if (!c->active) return -1;

  if (c->state == TCP_STATE_ESTABLISHED) {
    tcp_send_segment(c, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    c->state = TCP_STATE_FIN_WAIT_1;
  } else if (c->state == TCP_STATE_CLOSE_WAIT) {
    tcp_send_segment(c, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    c->state = TCP_STATE_LAST_ACK;
  } else {
    c->active = 0;
    c->state = TCP_STATE_CLOSED;
  }
  if (c->send_buf) { kfree(c->send_buf); c->send_buf = NULL; }
  if (c->recv_buf) { kfree(c->recv_buf); c->recv_buf = NULL; }
  c->send_cap = 0;
  c->recv_cap = 0;
  return 0;
}

void tcp_receive_segment(uint32_t src_ip, uint32_t dst_ip,
                         const uint8_t *segment, size_t len) {
  if (len < 20) return;
  (void)dst_ip;

  struct tcp_header *hdr = (struct tcp_header *)segment;
  uint16_t src_port = tcp_ntohs(hdr->src_port);
  uint16_t dst_port = tcp_ntohs(hdr->dst_port);
  uint32_t seq = tcp_ntohl(hdr->seq_num);
  uint32_t ack = tcp_ntohl(hdr->ack_num);
  uint8_t flags = hdr->flags;
  uint32_t data_off = ((uint32_t)(hdr->data_offset >> 4)) * 4;

  tcp_global_stats.segments_received++;

  struct tcp_connection *c = NULL;
  for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
    if (!tcp_conns[i].active) continue;
    if (tcp_conns[i].local_port == dst_port &&
        (tcp_conns[i].remote_port == src_port || tcp_conns[i].state == TCP_STATE_LISTEN) &&
        (tcp_conns[i].remote_ip == src_ip || tcp_conns[i].state == TCP_STATE_LISTEN)) {
      c = &tcp_conns[i];
      break;
    }
  }
  if (!c) return;

  switch (c->state) {
  case TCP_STATE_SYN_SENT:
    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
      c->irs = seq;
      c->rcv_nxt = seq + 1;
      c->snd_una = ack;
      c->state = TCP_STATE_ESTABLISHED;
      tcp_send_segment(c, TCP_FLAG_ACK, NULL, 0);
    }
    break;

  case TCP_STATE_LISTEN:
    if (flags & TCP_FLAG_SYN) {
      c->remote_ip = src_ip;
      c->remote_port = src_port;
      c->irs = seq;
      c->rcv_nxt = seq + 1;
      c->iss = tcp_gen_isn();
      c->snd_nxt = c->iss;
      c->snd_una = c->iss;
      c->state = TCP_STATE_SYN_RCVD;
      tcp_send_segment(c, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
    }
    break;

  case TCP_STATE_SYN_RCVD:
    if (flags & TCP_FLAG_ACK) {
      c->snd_una = ack;
      c->state = TCP_STATE_ESTABLISHED;
    }
    break;

  case TCP_STATE_ESTABLISHED:
    if (flags & TCP_FLAG_ACK) c->snd_una = ack;
    if (data_off < len) {
      size_t data_len = len - data_off;
      const uint8_t *payload = segment + data_off;
      if (c->recv_buf && c->recv_len + data_len <= c->recv_cap) {
        tcp_memcpy(c->recv_buf + c->recv_len, payload, data_len);
        c->recv_len += (uint32_t)data_len;
        c->rcv_nxt += (uint32_t)data_len;
      }
      tcp_send_segment(c, TCP_FLAG_ACK, NULL, 0);
    }
    if (flags & TCP_FLAG_FIN) {
      c->rcv_nxt++;
      c->state = TCP_STATE_CLOSE_WAIT;
      tcp_send_segment(c, TCP_FLAG_ACK, NULL, 0);
    }
    break;

  case TCP_STATE_FIN_WAIT_1:
    if (flags & TCP_FLAG_ACK) {
      c->snd_una = ack;
      if (flags & TCP_FLAG_FIN) {
        c->rcv_nxt++;
        c->state = TCP_STATE_TIME_WAIT;
        tcp_send_segment(c, TCP_FLAG_ACK, NULL, 0);
      } else {
        c->state = TCP_STATE_FIN_WAIT_2;
      }
    }
    break;

  case TCP_STATE_FIN_WAIT_2:
    if (flags & TCP_FLAG_FIN) {
      c->rcv_nxt++;
      c->state = TCP_STATE_TIME_WAIT;
      tcp_send_segment(c, TCP_FLAG_ACK, NULL, 0);
    }
    break;

  case TCP_STATE_LAST_ACK:
    if (flags & TCP_FLAG_ACK) {
      c->state = TCP_STATE_CLOSED;
      c->active = 0;
    }
    break;

  case TCP_STATE_TIME_WAIT:
    c->active = 0;
    c->state = TCP_STATE_CLOSED;
    break;

  default:
    break;
  }
}

void tcp_timer_tick(void) {
  for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
    struct tcp_connection *c = &tcp_conns[i];
    if (!c->active) continue;
    if (c->state == TCP_STATE_TIME_WAIT) {
      c->active = 0;
      c->state = TCP_STATE_CLOSED;
    }
  }
}

void tcp_stats_get(struct tcp_stats *out) {
  if (out) *out = tcp_global_stats;
}

int tcp_connection_state(int conn_id) {
  if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return TCP_STATE_CLOSED;
  return tcp_conns[conn_id].state;
}
