#include "internal/http_internal.h"

struct http_pool_entry g_http_pool[HTTP_POOL_MAX];

struct http_pool_entry *http_pool_find(const char *host,
                                       uint16_t port, int use_tls) {
  for (int i = 0; i < HTTP_POOL_MAX; i++) {
    struct http_pool_entry *e = &g_http_pool[i];
    if (!e->active) continue;
    if (e->port != port || e->use_tls != (uint8_t)use_tls) continue;
    if (http_streq_ci(e->host, host)) return e;
  }
  return NULL;
}

void http_pool_remove(struct http_pool_entry *e) {
  if (!e || !e->active) return;
  if (e->tls) { tls_close(e->tls); tls_free(e->tls); e->tls = NULL; }
  if (e->socket_fd >= 0) { socket_close(e->socket_fd); e->socket_fd = -1; }
  e->active = 0;
}

void http_pool_store(const char *host, uint16_t port, int use_tls,
                     int socket_fd, struct tls_context *tls) {
  struct http_pool_entry *slot = NULL;
  for (int i = 0; i < HTTP_POOL_MAX; i++) {
    if (!g_http_pool[i].active) { slot = &g_http_pool[i]; break; }
  }
  if (!slot) {
    http_pool_remove(&g_http_pool[0]);
    slot = &g_http_pool[0];
  }
  http_strcpy(slot->host, host, sizeof(slot->host));
  slot->port     = port;
  slot->use_tls  = (uint8_t)use_tls;
  slot->socket_fd = socket_fd;
  slot->tls      = tls;
  slot->active   = 1;
}

void http_transport_reset(struct http_transport *transport) {
  if (!transport) return;
  transport->socket_fd = -1;
  transport->use_tls = 0;
  transport->tls = NULL;
}

int http_transport_connect(struct http_transport *transport,
                           const struct http_request *req,
                           uint32_t ip) {
  struct sockaddr_in addr;
  struct tls_config tls_config;
  if (!transport || !req) return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  http_transport_reset(transport);

  {
    struct http_pool_entry *pooled = http_pool_find(req->host, req->port,
                                                    req->use_tls);
    if (pooled) {
      transport->socket_fd = pooled->socket_fd;
      transport->use_tls   = pooled->use_tls;
      transport->tls       = pooled->tls;
      pooled->socket_fd = -1;
      pooled->tls       = NULL;
      pooled->active    = 0;
      return 0;
    }
  }

  transport->socket_fd = socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (transport->socket_fd < 0) return http_fail(HTTP_ERR_SOCKET);

  http_memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = ((req->port >> 8) & 0xFF) | ((req->port << 8) & 0xFF00);
  addr.sin_addr = ip;

  if (req->timeout_ms > 0) {
    (void)socket_setsockopt(transport->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                            &req->timeout_ms, sizeof(req->timeout_ms));
    (void)socket_setsockopt(transport->socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                            &req->timeout_ms, sizeof(req->timeout_ms));
  }

  if (socket_connect(transport->socket_fd, &addr) != 0) {
    socket_close(transport->socket_fd);
    http_transport_reset(transport);
    return http_fail(HTTP_ERR_CONNECT);
  }

  transport->use_tls = req->use_tls;
  if (transport->use_tls) {
    http_memset(&tls_config, 0, sizeof(tls_config));
    tls_config.verify_peer = 1;
    tls_config.timeout_ms = req->timeout_ms;
    transport->tls = tls_connect(transport->socket_fd, req->host, &tls_config);
    if (!transport->tls) {
      socket_close(transport->socket_fd);
      http_transport_reset(transport);
      return http_fail(HTTP_ERR_TLS);
    }
  }
  return 0;
}

int http_transport_send(struct http_transport *transport,
                        const void *buf, size_t len) {
  if (!transport || !buf || len == 0) return 0;
  if (transport->use_tls) {
    return tls_send(transport->tls, buf, len);
  }
  return socket_send(transport->socket_fd, buf, len, 0);
}

int http_transport_send_all(struct http_transport *transport,
                            const void *buf, size_t len) {
  const uint8_t *data = (const uint8_t *)buf;
  size_t sent = 0;
  if (!transport || (!data && len > 0)) return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  while (sent < len) {
    int rc = http_transport_send(transport, data + sent, len - sent);
    if (rc <= 0) return http_fail(HTTP_ERR_SEND);
    sent += (size_t)rc;
  }
  return 0;
}

int http_transport_recv(struct http_transport *transport,
                        void *buf, size_t len) {
  if (!transport || !buf || len == 0) return -1;
  if (transport->use_tls) {
    return tls_recv(transport->tls, buf, len);
  }
  return socket_recv(transport->socket_fd, buf, len, 0);
}

void http_transport_close(struct http_transport *transport) {
  if (!transport) return;
  if (transport->tls) {
    tls_close(transport->tls);
    tls_free(transport->tls);
    transport->tls = NULL;
  }
  if (transport->socket_fd >= 0) {
    socket_close(transport->socket_fd);
  }
  http_transport_reset(transport);
}
