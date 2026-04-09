#ifndef NET_TCP_H
#define NET_TCP_H

#include <stdint.h>
#include <stddef.h>

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

#define TCP_STATE_CLOSED      0
#define TCP_STATE_LISTEN      1
#define TCP_STATE_SYN_SENT    2
#define TCP_STATE_SYN_RCVD    3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT_1  5
#define TCP_STATE_FIN_WAIT_2  6
#define TCP_STATE_CLOSE_WAIT  7
#define TCP_STATE_CLOSING     8
#define TCP_STATE_LAST_ACK    9
#define TCP_STATE_TIME_WAIT   10

#define TCP_WINDOW_SIZE  1024
#define TCP_MSS_DEFAULT  1460
#define TCP_MAX_RETRIES  5
#define TCP_RTO_INIT_MS  1000
#define TCP_RTO_MAX_MS   60000
#define TCP_MAX_CONNECTIONS 8

struct tcp_header {
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t seq_num;
  uint32_t ack_num;
  uint8_t  data_offset;
  uint8_t  flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent;
} __attribute__((packed));

struct tcp_connection {
  uint32_t local_ip;
  uint16_t local_port;
  uint32_t remote_ip;
  uint16_t remote_port;
  uint8_t  state;
  uint32_t snd_una;
  uint32_t snd_nxt;
  uint32_t snd_wnd;
  uint32_t rcv_nxt;
  uint32_t rcv_wnd;
  uint32_t iss;
  uint32_t irs;
  uint16_t mss;
  uint32_t rto_ms;
  uint32_t retries;
  uint64_t last_send_tick;
  uint8_t  *send_buf;
  uint32_t send_len;
  uint32_t send_cap;
  uint8_t  *recv_buf;
  uint32_t recv_len;
  uint32_t recv_cap;
  uint32_t recv_head;
  int socket_fd;
  int active;
};

struct tcp_stats {
  uint64_t segments_sent;
  uint64_t segments_received;
  uint64_t retransmits;
  uint64_t resets;
  uint32_t active_connections;
};

void tcp_init(void);
int tcp_open(uint32_t local_ip, uint16_t local_port,
             uint32_t remote_ip, uint16_t remote_port, int socket_fd);
int tcp_listen(uint32_t local_ip, uint16_t local_port, int socket_fd);
int tcp_send(int conn_id, const void *data, size_t len);
int tcp_recv(int conn_id, void *buf, size_t len);
int tcp_close(int conn_id);
void tcp_receive_segment(uint32_t src_ip, uint32_t dst_ip,
                         const uint8_t *segment, size_t len);
void tcp_timer_tick(void);
void tcp_stats_get(struct tcp_stats *out);
int tcp_connection_state(int conn_id);

#endif /* NET_TCP_H */
