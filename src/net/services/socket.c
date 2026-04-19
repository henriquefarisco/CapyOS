#include "net/socket.h"
#include "net/stack.h"
#include "net/tcp.h"
#include "memory/kmem.h"
#include "../internal/stack_utils.h"
#include <stddef.h>

static struct socket socket_table[SOCKET_MAX];
static struct socket_stats sock_stats;
static int socket_initialized = 1;

static void socket_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static int socket_wait_for_tcp_state(int conn_id, uint8_t target_state,
                                     uint32_t timeout_ms) {
  uint32_t next_retransmit_at = TCP_RTO_INIT_MS;
  uint32_t retransmit_count = 0;
  for (uint32_t elapsed = 0; elapsed <= timeout_ms; ++elapsed) {
    int state = tcp_connection_state(conn_id);
    if (state == target_state) {
      return 0;
    }
    if (state == TCP_STATE_CLOSED || state == TCP_STATE_CLOSE_WAIT ||
        state == TCP_STATE_CLOSING || state == TCP_STATE_LAST_ACK ||
        state == TCP_STATE_TIME_WAIT) {
      return -1;
    }
    /* Real networks drop packets; the SYN or SYN-ACK can vanish. Retransmit
       the SYN with exponential backoff (1s, 2s, 4s, ...) so the connect does
       not fail on a single drop. */
    if (state == TCP_STATE_SYN_SENT && elapsed >= next_retransmit_at &&
        retransmit_count < TCP_MAX_RETRIES) {
      tcp_retransmit_syn(conn_id);
      retransmit_count++;
      next_retransmit_at = elapsed + (TCP_RTO_INIT_MS << retransmit_count);
    }
    (void)net_stack_poll();
    net_stack_delay_approx_1ms();
  }
  return -1;
}

void socket_system_init(void) {
  socket_memset(socket_table, 0, sizeof(socket_table));
  socket_memset(&sock_stats, 0, sizeof(sock_stats));
  socket_initialized = 1;
}

static struct socket *socket_get(int fd) {
  if (fd < 0 || fd >= SOCKET_MAX) return NULL;
  if (socket_table[fd].state == SOCK_STATE_UNUSED) return NULL;
  return &socket_table[fd];
}

int socket_create(int domain, int type, int protocol) {
  if (!socket_initialized) return -1;
  for (int i = 0; i < SOCKET_MAX; i++) {
    if (socket_table[i].state == SOCK_STATE_UNUSED) {
      struct socket *s = &socket_table[i];
      socket_memset(s, 0, sizeof(*s));
      s->fd = i;
      s->domain = (uint16_t)domain;
      s->type = (uint16_t)type;
      s->protocol = (uint16_t)(protocol ? protocol :
        (type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP));
      s->state = SOCK_STATE_CREATED;
      s->timeout_ms = 5000;
      s->recv_buf = (uint8_t *)kmalloc(SOCKET_BUF_SIZE);
      s->send_buf = (uint8_t *)kmalloc(SOCKET_BUF_SIZE);
      s->recv_cap = s->recv_buf ? SOCKET_BUF_SIZE : 0;
      s->send_cap = s->send_buf ? SOCKET_BUF_SIZE : 0;
      sock_stats.active_sockets++;
      return i;
    }
  }
  return -1;
}

int socket_bind(int fd, const struct sockaddr_in *addr) {
  struct socket *s = socket_get(fd);
  if (!s || !addr) return -1;
  if (s->state != SOCK_STATE_CREATED) return -1;
  s->local_addr = *addr;
  s->state = SOCK_STATE_BOUND;
  return 0;
}

int socket_listen(int fd, int backlog) {
  struct socket *s = socket_get(fd);
  if (!s) return -1;
  if (s->state != SOCK_STATE_BOUND) return -1;
  if (s->type != SOCK_STREAM) return -1;
  s->backlog = backlog > 0 ? backlog : 1;
  s->state = SOCK_STATE_LISTENING;
  return 0;
}

int socket_accept(int fd, struct sockaddr_in *addr) {
  struct socket *s = socket_get(fd);
  if (!s || s->state != SOCK_STATE_LISTENING) return -1;
  (void)addr;
  return -1;
}

int socket_connect(int fd, const struct sockaddr_in *addr) {
  struct socket *s = socket_get(fd);
  if (!s || !addr) return -1;
  s->remote_addr = *addr;
  s->state = SOCK_STATE_CONNECTING;

  if (s->type == SOCK_DGRAM) {
    s->state = SOCK_STATE_CONNECTED;
    sock_stats.connections++;
    return 0;
  }

  if (s->type == SOCK_STREAM) {
    struct net_ipv4_config ipv4;
    struct net_stack_status st;
    net_stack_status(&st);
    ipv4 = st.ipv4;

    if (s->local_addr.sin_port == 0) {
      static uint16_t ephemeral = 49152;
      s->local_addr.sin_port = ephemeral++;
      if (ephemeral == 0) ephemeral = 49152;
      s->local_addr.sin_addr = ipv4.addr;
    }

    /* sin_port is in network byte order (BSD convention); tcp_open expects
       host byte order because tcp_send_segment will call tcp_htons() on it. */
    uint16_t rp = addr->sin_port;
    uint16_t rp_h = (uint16_t)((rp >> 8) | ((rp & 0xFF) << 8));
    int conn = tcp_open(s->local_addr.sin_addr, s->local_addr.sin_port,
                        addr->sin_addr, rp_h, fd);
    if (conn < 0) {
      s->state = SOCK_STATE_ERROR;
      s->error = conn;
      return -1;
    }
    s->protocol_data = (void *)(uintptr_t)conn;
    if (socket_wait_for_tcp_state(conn, TCP_STATE_ESTABLISHED,
                                  s->timeout_ms) != 0) {
      tcp_close(conn);
      s->protocol_data = NULL;
      s->state = SOCK_STATE_ERROR;
      s->error = -1;
      return -1;
    }
    s->state = SOCK_STATE_CONNECTED;
    sock_stats.connections++;
    return 0;
  }
  return -1;
}

int socket_send(int fd, const void *buf, size_t len, int flags) {
  struct socket *s = socket_get(fd);
  if (!s || s->state != SOCK_STATE_CONNECTED) return -1;
  (void)flags;

  if (s->type == SOCK_STREAM) {
    int conn = (int)(uintptr_t)s->protocol_data;
    if (socket_wait_for_tcp_state(conn, TCP_STATE_ESTABLISHED,
                                  s->timeout_ms) != 0) {
      s->error = -1;
      return -1;
    }
    int r = tcp_send(conn, buf, len);
    if (r >= 0) sock_stats.bytes_sent += (uint64_t)r;
    return r;
  }

  if (s->type == SOCK_DGRAM) {
    const uint8_t *data = (const uint8_t *)buf;
    uint16_t src_port = s->local_addr.sin_port;
    uint16_t dst_port = s->remote_addr.sin_port;

    uint8_t udp_pkt[8 + SOCKET_BUF_SIZE];
    if (len > SOCKET_BUF_SIZE) len = SOCKET_BUF_SIZE;
    uint16_t total = (uint16_t)(8 + len);
    udp_pkt[0] = (uint8_t)(src_port >> 8);
    udp_pkt[1] = (uint8_t)src_port;
    udp_pkt[2] = (uint8_t)(dst_port >> 8);
    udp_pkt[3] = (uint8_t)dst_port;
    udp_pkt[4] = (uint8_t)(total >> 8);
    udp_pkt[5] = (uint8_t)total;
    udp_pkt[6] = 0; udp_pkt[7] = 0;
    for (size_t i = 0; i < len; i++) udp_pkt[8 + i] = data[i];

    int r = net_stack_send_ipv4(NET_L4_PROTO_UDP, s->remote_addr.sin_addr,
                                 udp_pkt, total);
    if (r == 0) { sock_stats.bytes_sent += len; return (int)len; }
    return -1;
  }
  return -1;
}

int socket_recv(int fd, void *buf, size_t len, int flags) {
  struct socket *s = socket_get(fd);
  if (!s || s->state != SOCK_STATE_CONNECTED) return -1;
  (void)flags;

  if (s->type == SOCK_STREAM) {
    int conn = (int)(uintptr_t)s->protocol_data;
    int state;
    for (uint32_t elapsed = 0; elapsed <= s->timeout_ms; ++elapsed) {
      int r;
      if (tcp_available(conn) == 0) {
        state = tcp_connection_state(conn);
        if (state == TCP_STATE_CLOSED ||
            state == TCP_STATE_CLOSE_WAIT ||
            state == TCP_STATE_TIME_WAIT) {
          return 0;
        }
        (void)net_stack_poll();
        net_stack_delay_approx_1ms();
        continue;
      }
      r = tcp_recv(conn, buf, len);
      if (r >= 0) {
        if (r > 0) sock_stats.bytes_received += (uint64_t)r;
        return r;
      }
      state = tcp_connection_state(conn);
      if (state == TCP_STATE_CLOSED ||
          state == TCP_STATE_CLOSE_WAIT ||
          state == TCP_STATE_TIME_WAIT) {
        return 0;
      }
      (void)net_stack_poll();
      net_stack_delay_approx_1ms();
    }
    s->error = -1;
    return -1;
  }

  if (s->type == SOCK_DGRAM) {
    if (s->recv_len == 0 || !s->recv_buf) return 0;
    uint32_t avail = s->recv_len;
    if (avail > len) avail = (uint32_t)len;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < avail; i++) {
      dst[i] = s->recv_buf[(s->recv_tail + i) % s->recv_cap];
    }
    s->recv_tail = (s->recv_tail + avail) % s->recv_cap;
    s->recv_len -= avail;
    sock_stats.bytes_received += avail;
    return (int)avail;
  }
  return -1;
}

int socket_sendto(int fd, const void *buf, size_t len, int flags,
                  const struct sockaddr_in *addr) {
  struct socket *s = socket_get(fd);
  if (!s || !addr) return -1;
  struct sockaddr_in saved = s->remote_addr;
  s->remote_addr = *addr;
  if (s->state == SOCK_STATE_CREATED || s->state == SOCK_STATE_BOUND)
    s->state = SOCK_STATE_CONNECTED;
  int r = socket_send(fd, buf, len, flags);
  s->remote_addr = saved;
  return r;
}

int socket_recvfrom(int fd, void *buf, size_t len, int flags,
                    struct sockaddr_in *addr) {
  int r = socket_recv(fd, buf, len, flags);
  if (r > 0 && addr) {
    struct socket *s = socket_get(fd);
    if (s) *addr = s->remote_addr;
  }
  return r;
}

int socket_close(int fd) {
  struct socket *s = socket_get(fd);
  if (!s) return -1;

  if (s->type == SOCK_STREAM && s->protocol_data) {
    extern int tcp_close(int);
    tcp_close((int)(uintptr_t)s->protocol_data);
  }

  if (s->recv_buf) { kfree(s->recv_buf); s->recv_buf = NULL; }
  if (s->send_buf) { kfree(s->send_buf); s->send_buf = NULL; }
  s->recv_cap = 0;
  s->send_cap = 0;
  s->state = SOCK_STATE_UNUSED;
  if (sock_stats.active_sockets > 0) sock_stats.active_sockets--;
  return 0;
}

int socket_setsockopt(int fd, int level, int optname, const void *optval,
                      uint32_t optlen) {
  struct socket *s = socket_get(fd);
  if (!s || !optval) return -1;
  (void)level;
  if ((optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
      optlen >= sizeof(uint32_t)) {
    s->timeout_ms = *(const uint32_t *)optval;
    return 0;
  }
  return -1;
}

int socket_getsockopt(int fd, int level, int optname, void *optval,
                      uint32_t *optlen) {
  struct socket *s = socket_get(fd);
  if (!s || !optval || !optlen) return -1;
  (void)level;
  if (optname == SO_ERROR && *optlen >= sizeof(int)) {
    *(int *)optval = s->error;
    *optlen = sizeof(int);
    return 0;
  }
  return -1;
}

int socket_shutdown(int fd, int how) {
  struct socket *s = socket_get(fd);
  if (!s) return -1;
  (void)how;
  s->state = SOCK_STATE_CLOSING;
  return 0;
}

void socket_stats_get(struct socket_stats *out) {
  if (out) *out = sock_stats;
}

int socket_poll_ready(int fd, int events, uint32_t timeout_ms) {
  struct socket *s = socket_get(fd);
  if (!s) return -1;
  (void)events;
  for (uint32_t elapsed = 0; elapsed <= timeout_ms; ++elapsed) {
    if (s->type == SOCK_STREAM && s->protocol_data) {
      int conn = (int)(uintptr_t)s->protocol_data;
      int state = tcp_connection_state(conn);
      if (tcp_available(conn) > 0 ||
          state == TCP_STATE_CLOSED ||
          state == TCP_STATE_CLOSE_WAIT ||
          state == TCP_STATE_TIME_WAIT) {
        return 1;
      }
    } else if (s->recv_len > 0) {
      return 1;
    }
    (void)net_stack_poll();
    if (elapsed != timeout_ms) {
      net_stack_delay_approx_1ms();
    }
  }
  return 0;
}

void socket_receive_packet(uint8_t protocol, uint32_t src_ip,
                           uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, size_t len) {
  for (int i = 0; i < SOCKET_MAX; i++) {
    struct socket *s = &socket_table[i];
    if (s->state == SOCK_STATE_UNUSED) continue;
    if (s->local_addr.sin_port != dst_port) continue;
    if (s->protocol == protocol || s->protocol == 0) {
      s->remote_addr.sin_addr = src_ip;
      s->remote_addr.sin_port = src_port;
      if (!s->recv_buf || s->recv_cap == 0) return;
      for (size_t j = 0; j < len && s->recv_len < s->recv_cap; j++) {
        s->recv_buf[s->recv_head] = data[j];
        s->recv_head = (s->recv_head + 1) % s->recv_cap;
        s->recv_len++;
      }
      return;
    }
  }
}
