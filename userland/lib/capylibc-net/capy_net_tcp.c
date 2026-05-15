/*
 * userland/lib/capylibc-net/capy_net_tcp.c (F4 seção c parte 2/2)
 *
 * High-level TCP client helpers. Built directly on top of the
 * raw `capy_socket / capy_connect / capy_send / capy_recv /
 * capy_close` syscall stubs (see userland/lib/capylibc/
 * syscall_stubs.S).
 *
 * Design notes:
 *
 *   - capy_tcp_connect_ip4 owns full rollback: if any step after
 *     capy_socket fails, the socket fd is closed via capy_close
 *     so the caller never has to worry about a half-open FD slot.
 *
 *   - capy_send_all loops until the kernel accepts every byte.
 *     A send returning 0 indicates "kernel buffer full and no
 *     forward progress this iteration"; we treat that as a hard
 *     stop and bubble up the partial count rather than spinning.
 *     A send returning -1 likewise stops the loop. The callers
 *     that need true "block forever" semantics (e.g. browser
 *     fetch streaming) can detect partial returns and re-call
 *     capy_send_all on the tail.
 *
 *   - capy_recv_until reads ONE byte at a time. That is wasteful
 *     in syscall count for high-throughput workloads but exactly
 *     right for line-oriented protocols (HTTP request parsing,
 *     SMTP-style line greetings). Bulk recv is covered by
 *     capy_recv_all with a fixed cap.
 *
 *   - The kernel-side socket layer documents `capy_send` and
 *     `capy_recv` as `long` returning bytes, and `capy_socket /
 *     bind / connect / listen / accept` as `int`. We adhere to
 *     those signatures exactly.
 */

#include "capylibc-net/capy_net.h"
#include "capylibc/capylibc.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);

/* The userland syscall stubs declared in capylibc.h (capy_socket,
 * capy_connect, capy_send, capy_recv) live in syscall_stubs.S and
 * forward register-shuffled arguments to the kernel. The host
 * unit-test harness for libcapy-net provides C replacements for
 * those stubs, recorded in tests/test_capylibc_net.c. The
 * libcapy-net code itself never observes the difference. */

int capy_tcp_connect_ip4(uint32_t ip_host_order, uint16_t port_host_order) {
  capy_net_internal_reset_error();

  int fd = capy_socket(2 /*AF_INET*/, 1 /*SOCK_STREAM*/, 0);
  if (fd < 0) {
    capy_net_internal_set_error(CAPY_NET_ESOCK);
    return -1;
  }

  struct capy_sockaddr_in addr;
  addr.sin_family = 2 /*AF_INET*/;
  addr.sin_port   = capy_htons(port_host_order);
  addr.sin_addr   = capy_htonl(ip_host_order);
  for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

  if (capy_connect(fd, &addr, (unsigned int)sizeof(addr)) != 0) {
    /* Roll back the partially-allocated FD so no slot leaks on
     * connect failure; the kernel close hook handles socket
     * teardown for us via process_fd_register_socket_close. */
    (void)capy_close(fd);
    capy_net_internal_set_error(CAPY_NET_ECONNECT);
    return -1;
  }

  return fd;
}

int capy_tcp_connect_str(const char *ip_dotted, uint16_t port_host_order) {
  capy_net_internal_reset_error();
  if (!ip_dotted) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  uint32_t ip_host_order = 0;
  if (capy_inet_pton4(ip_dotted, &ip_host_order) != 0) {
    /* capy_inet_pton4 already populated the error code
     * (CAPY_NET_EPARSE / CAPY_NET_EINVAL); just propagate. */
    return -1;
  }
  return capy_tcp_connect_ip4(ip_host_order, port_host_order);
}

long capy_send_all(int fd, const void *buf, size_t len) {
  capy_net_internal_reset_error();
  if (fd < 0 || (!buf && len > 0)) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  if (len == 0) return 0;

  const uint8_t *p = (const uint8_t *)buf;
  size_t total = 0;
  while (total < len) {
    long n = capy_send(fd, p + total, len - total, 0);
    if (n < 0) {
      capy_net_internal_set_error(CAPY_NET_ESEND);
      /* Return the partial count if any bytes already moved.
       * Returning 0 here would be ambiguous with "successfully
       * sent zero bytes"; -1 is the documented signal for "no
       * progress AND error", and the caller can check
       * capy_net_last_error() to distinguish. */
      return total > 0 ? (long)total : -1;
    }
    if (n == 0) {
      /* Kernel accepted zero bytes: no forward progress. Stop. */
      capy_net_internal_set_error(CAPY_NET_ESEND);
      return (long)total;
    }
    total += (size_t)n;
  }
  return (long)total;
}

long capy_recv_all(int fd, void *buf, size_t cap) {
  capy_net_internal_reset_error();
  if (fd < 0 || (!buf && cap > 0)) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  if (cap == 0) return 0;

  long n = capy_recv(fd, buf, cap, 0);
  if (n < 0) {
    capy_net_internal_set_error(CAPY_NET_ERECV);
    return -1;
  }
  return n;
}

long capy_recv_until(int fd, void *buf, size_t cap, uint8_t terminator) {
  capy_net_internal_reset_error();
  if (fd < 0 || !buf || cap == 0) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  uint8_t *p = (uint8_t *)buf;
  size_t total = 0;
  while (total < cap) {
    uint8_t one = 0;
    long n = capy_recv(fd, &one, 1, 0);
    if (n < 0) {
      capy_net_internal_set_error(CAPY_NET_ERECV);
      return total > 0 ? (long)total : -1;
    }
    if (n == 0) {
      /* Clean EOF: report whatever we already buffered. */
      return (long)total;
    }
    p[total++] = one;
    if (one == terminator) return (long)total;
  }
  /* cap exhausted without seeing terminator. */
  return (long)total;
}
