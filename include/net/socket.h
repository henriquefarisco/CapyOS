#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_ICMP 1

#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_RCVBUF   8
#define SO_SNDBUF   7
#define SO_ERROR    4

#define SOCKET_MAX 16
#define SOCKET_BUF_SIZE 512

#define MSG_DONTWAIT 0x40
#define MSG_PEEK     0x02

struct sockaddr {
  uint16_t sa_family;
  uint8_t  sa_data[14];
};

struct sockaddr_in {
  uint16_t sin_family;
  uint16_t sin_port;
  uint32_t sin_addr;
  uint8_t  sin_zero[8];
};

enum socket_state {
  SOCK_STATE_UNUSED = 0,
  SOCK_STATE_CREATED,
  SOCK_STATE_BOUND,
  SOCK_STATE_LISTENING,
  SOCK_STATE_CONNECTING,
  SOCK_STATE_CONNECTED,
  SOCK_STATE_CLOSING,
  SOCK_STATE_CLOSED,
  SOCK_STATE_ERROR
};

struct socket {
  int fd;
  uint16_t domain;
  uint16_t type;
  uint16_t protocol;
  enum socket_state state;
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;
  uint8_t *recv_buf;
  uint32_t recv_head;
  uint32_t recv_tail;
  uint32_t recv_len;
  uint32_t recv_cap;
  uint8_t *send_buf;
  uint32_t send_head;
  uint32_t send_tail;
  uint32_t send_len;
  uint32_t send_cap;
  uint32_t timeout_ms;
  int error;
  int backlog;
  void *protocol_data;
};

struct socket_stats {
  uint32_t active_sockets;
  uint64_t bytes_sent;
  uint64_t bytes_received;
  uint64_t connections;
  uint64_t errors;
};

void socket_system_init(void);
int socket_create(int domain, int type, int protocol);
int socket_bind(int fd, const struct sockaddr_in *addr);
int socket_listen(int fd, int backlog);
int socket_accept(int fd, struct sockaddr_in *addr);
int socket_connect(int fd, const struct sockaddr_in *addr);
int socket_send(int fd, const void *buf, size_t len, int flags);
int socket_recv(int fd, void *buf, size_t len, int flags);
int socket_sendto(int fd, const void *buf, size_t len, int flags,
                  const struct sockaddr_in *addr);
int socket_recvfrom(int fd, void *buf, size_t len, int flags,
                    struct sockaddr_in *addr);
int socket_close(int fd);
int socket_setsockopt(int fd, int level, int optname, const void *optval,
                      uint32_t optlen);
int socket_getsockopt(int fd, int level, int optname, void *optval,
                      uint32_t *optlen);
int socket_shutdown(int fd, int how);
void socket_stats_get(struct socket_stats *out);
int socket_poll_ready(int fd, int events, uint32_t timeout_ms);
void socket_receive_packet(uint8_t protocol, uint32_t src_ip, uint16_t src_port,
                           uint16_t dst_port, const uint8_t *data, size_t len);

#endif /* NET_SOCKET_H */
