#include "net/http.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "net/stack.h"
#include "net/http_encoding.h"
#include "security/tls.h"
#include "memory/kmem.h"
#include <stddef.h>

static void http_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void http_memcpy(void *d, const void *s, size_t n) {
  uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
  for (size_t i = 0; i < n; i++) dp[i] = sp[i];
}
static size_t http_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static char http_tolower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}
static void http_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static int http_buf_append_char(char *buf, size_t buf_size, size_t *pos, char ch) {
  if (!buf || !pos || *pos + 1 >= buf_size) return -1;
  buf[(*pos)++] = ch;
  return 0;
}
static int http_buf_append_str(char *buf, size_t buf_size, size_t *pos,
                               const char *str) {
  if (!buf || !pos || !str) return -1;
  while (*str) {
    if (http_buf_append_char(buf, buf_size, pos, *str++) != 0) return -1;
  }
  return 0;
}
static int http_buf_append_u32(char *buf, size_t buf_size, size_t *pos,
                               uint32_t value) {
  char digits[10];
  size_t count = 0;
  if (!buf || !pos) return -1;
  if (value == 0) return http_buf_append_char(buf, buf_size, pos, '0');
  while (value > 0 && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10));
    value /= 10;
  }
  while (count > 0) {
    if (http_buf_append_char(buf, buf_size, pos, digits[--count]) != 0) return -1;
  }
  return 0;
}
static int http_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}
static int http_streq_ci(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] || b[i]) {
    if (http_tolower(a[i]) != http_tolower(b[i])) return 0;
    i++;
  }
  return 1;
}
static int http_contains_ci(const char *text, const char *needle) {
  size_t i = 0;
  size_t needle_len = http_strlen(needle);
  if (!text || !needle || needle_len == 0) return 0;
  while (text[i]) {
    size_t j = 0;
    while (j < needle_len && text[i + j] &&
           http_tolower(text[i + j]) == http_tolower(needle[j])) j++;
    if (j == needle_len) return 1;
    i++;
  }
  return 0;
}

static int http_is_default_port(const struct http_request *req) {
  if (!req) return 1;
  return (req->use_tls && req->port == 443) || (!req->use_tls && req->port == 80);
}

static int http_request_has_header(const struct http_request *req, const char *name) {
  if (!req || !name) return 0;
  for (uint32_t i = 0; i < req->header_count; i++) {
    if (http_streq_ci(req->headers[i].name, name)) {
      return 1;
    }
  }
  return 0;
}

enum http_error {
  HTTP_OK = 0,
  HTTP_ERR_INVALID_ARGUMENT = 1,
  HTTP_ERR_INVALID_URL,
  HTTP_ERR_DNS,
  HTTP_ERR_SOCKET,
  HTTP_ERR_CONNECT,
  HTTP_ERR_TLS,
  HTTP_ERR_SEND,
  HTTP_ERR_RECV,
  HTTP_ERR_RESPONSE_TOO_LARGE,
  HTTP_ERR_RESPONSE_ENCODING,
  HTTP_ERR_RESPONSE_PARSE,
  HTTP_ERR_NO_MEMORY,
  HTTP_ERR_REDIRECT_LIMIT,
  HTTP_ERR_BAD_REDIRECT
};

static int g_http_last_error = HTTP_OK;

static void http_set_ok(void) {
  g_http_last_error = HTTP_OK;
}

static int http_fail(int error) {
  g_http_last_error = error;
  return -error;
}

int http_last_error(void) {
  return g_http_last_error;
}

const char *http_error_string(int error) {
  switch (error < 0 ? -error : error) {
    case HTTP_OK: return "ok";
    case HTTP_ERR_INVALID_ARGUMENT: return "invalid argument";
    case HTTP_ERR_INVALID_URL: return "invalid url";
    case HTTP_ERR_DNS: return "dns resolution failed";
    case HTTP_ERR_SOCKET: return "socket creation failed";
    case HTTP_ERR_CONNECT: return "connection failed";
    case HTTP_ERR_TLS: return "tls handshake failed";
    case HTTP_ERR_SEND: return "request send failed";
    case HTTP_ERR_RECV: return "response receive failed";
    case HTTP_ERR_RESPONSE_TOO_LARGE: return "response too large";
    case HTTP_ERR_RESPONSE_ENCODING: return "unsupported response encoding";
    case HTTP_ERR_RESPONSE_PARSE: return "response parse failed";
    case HTTP_ERR_NO_MEMORY: return "out of memory";
    case HTTP_ERR_REDIRECT_LIMIT: return "too many redirects";
    case HTTP_ERR_BAD_REDIRECT: return "invalid redirect target";
    default: return "network error";
  }
}

static void http_store_headers(const char *headers, size_t len,
                               struct http_response *resp) {
  size_t pos = 0;
  if (!headers || !resp) return;
  while (pos + 1 < len) {
    if (headers[pos] == '\r' && headers[pos + 1] == '\n') {
      pos += 2;
      break;
    }
    pos++;
  }
  while (pos + 1 < len && resp->header_count < HTTP_MAX_HEADERS) {
    size_t line_start = pos;
    size_t line_end = pos;
    size_t colon = 0;
    struct http_header *hdr = NULL;
    if (headers[pos] == '\r' && headers[pos + 1] == '\n') break;
    while (line_end + 1 < len &&
           !(headers[line_end] == '\r' && headers[line_end + 1] == '\n')) {
      line_end++;
    }
    colon = line_start;
    while (colon < line_end && headers[colon] != ':') colon++;
    if (colon < line_end) {
      size_t name_len = colon - line_start;
      size_t value_start = colon + 1;
      size_t value_len = 0;
      while (value_start < line_end && headers[value_start] == ' ') value_start++;
      value_len = line_end - value_start;
      hdr = &resp->headers[resp->header_count++];
      if (name_len >= sizeof(hdr->name)) name_len = sizeof(hdr->name) - 1;
      if (value_len >= sizeof(hdr->value)) value_len = sizeof(hdr->value) - 1;
      for (size_t i = 0; i < name_len; i++) hdr->name[i] = headers[line_start + i];
      hdr->name[name_len] = '\0';
      for (size_t i = 0; i < value_len; i++) hdr->value[i] = headers[value_start + i];
      hdr->value[value_len] = '\0';
      if (http_streq_ci(hdr->name, "Transfer-Encoding") &&
          http_contains_ci(hdr->value, "chunked")) {
        resp->chunked = 1;
      }
    }
    pos = line_end;
    if (pos + 1 < len && headers[pos] == '\r' && headers[pos + 1] == '\n') pos += 2;
  }
}

static int http_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  ch = http_tolower(ch);
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  return -1;
}

static int http_parse_chunk_size(const uint8_t *buf, size_t len,
                                 size_t *header_len, size_t *chunk_size) {
  size_t i = 0;
  size_t size = 0;
  int saw_digit = 0;
  if (!buf || !header_len || !chunk_size) return -1;
  while (i < len) {
    int hex = http_hex_digit((char)buf[i]);
    if (hex >= 0) {
      size = (size << 4) | (size_t)hex;
      saw_digit = 1;
      i++;
      continue;
    }
    if (buf[i] == ';') {
      while (i + 1 < len && !(buf[i] == '\r' && buf[i + 1] == '\n')) i++;
    }
    if (i + 1 < len && buf[i] == '\r' && buf[i + 1] == '\n' && saw_digit) {
      *header_len = i + 2;
      *chunk_size = size;
      return 0;
    }
    return -1;
  }
  return -1;
}

static int http_chunked_complete(const uint8_t *buf, size_t len) {
  size_t pos = 0;
  if (!buf) return 0;
  while (pos < len) {
    size_t header_len = 0;
    size_t chunk_size = 0;
    if (http_parse_chunk_size(buf + pos, len - pos, &header_len, &chunk_size) != 0)
      return 0;
    pos += header_len;
    if (chunk_size == 0) {
      return (pos + 1 < len && buf[pos] == '\r' && buf[pos + 1] == '\n');
    }
    if (pos + chunk_size + 1 >= len) return 0;
    pos += chunk_size;
    if (buf[pos] != '\r' || buf[pos + 1] != '\n') return 0;
    pos += 2;
  }
  return 0;
}

static int http_decode_chunked_body(uint8_t *body, size_t *body_len) {
  size_t src = 0;
  size_t dst = 0;
  size_t len = body_len ? *body_len : 0;
  if (!body || !body_len) return -1;
  while (src < len) {
    size_t header_len = 0;
    size_t chunk_size = 0;
    if (http_parse_chunk_size(body + src, len - src, &header_len, &chunk_size) != 0)
      return -1;
    src += header_len;
    if (chunk_size == 0) {
      *body_len = dst;
      body[dst] = '\0';
      return 0;
    }
    if (src + chunk_size > len) return -1;
    http_memcpy(body + dst, body + src, chunk_size);
    dst += chunk_size;
    src += chunk_size;
    if (src + 1 >= len || body[src] != '\r' || body[src + 1] != '\n') return -1;
    src += 2;
  }
  return -1;
}

static char *http_grow_recv_buffer(char *buf, size_t current_capacity,
                                   size_t min_capacity,
                                   size_t *new_capacity_out) {
  size_t new_capacity = current_capacity;
  char *new_buf = NULL;
  if (!buf || !new_capacity_out) return NULL;
  if (min_capacity <= current_capacity) {
    *new_capacity_out = current_capacity;
    return buf;
  }
  while (new_capacity < min_capacity && new_capacity < HTTP_MAX_RESPONSE_SIZE) {
    new_capacity *= 2;
    if (new_capacity > HTTP_MAX_RESPONSE_SIZE) {
      new_capacity = HTTP_MAX_RESPONSE_SIZE;
      break;
    }
  }
  if (new_capacity < min_capacity || new_capacity <= current_capacity) return NULL;
  new_buf = (char *)kmalloc(new_capacity);
  if (!new_buf) return NULL;
  http_memcpy(new_buf, buf, current_capacity);
  kfree(buf);
  *new_capacity_out = new_capacity;
  return new_buf;
}

int http_init(void) {
  http_set_ok();
  return 0;
}

int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls) {
  http_set_ok();
  if (!url || !host || host_len == 0 || !path || path_len == 0 || !port || !use_tls) {
    return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  }
  *use_tls = 0;
  *port = 80;

  if (http_strncmp(url, "https://", 8) == 0) {
    *use_tls = 1; *port = 443; url += 8;
  } else if (http_strncmp(url, "http://", 7) == 0) {
    url += 7;
  }

  size_t hi = 0;
  while (url[hi] && url[hi] != '/' && url[hi] != ':' && hi < host_len - 1) {
    host[hi] = url[hi]; hi++;
  }
  host[hi] = '\0';
  if (hi == 0) return http_fail(HTTP_ERR_INVALID_URL);
  url += hi;

  if (*url == ':') {
    int saw_digit = 0;
    url++;
    uint16_t p = 0;
    while (*url >= '0' && *url <= '9') {
      saw_digit = 1;
      p = p * 10 + (uint16_t)(*url - '0');
      url++;
    }
    if (!saw_digit) return http_fail(HTTP_ERR_INVALID_URL);
    if (p > 0) *port = p;
  }

  if (*url == '/') http_strcpy(path, url, path_len);
  else http_strcpy(path, "/", path_len);

  return 0;
}

static int http_build_request(const struct http_request *req, char *buf, size_t buf_size) {
  const char *method_str = "GET";
  size_t pos = 0;

  if (!req || !buf || buf_size < 8) {
    return -1;
  }

  switch (req->method) {
    case HTTP_POST: method_str = "POST"; break;
    case HTTP_PUT: method_str = "PUT"; break;
    case HTTP_DELETE: method_str = "DELETE"; break;
    case HTTP_HEAD: method_str = "HEAD"; break;
    default: break;
  }

  if (http_buf_append_str(buf, buf_size, &pos, method_str) != 0 ||
      http_buf_append_char(buf, buf_size, &pos, ' ') != 0 ||
      http_buf_append_str(buf, buf_size, &pos, req->path[0] ? req->path : "/") != 0 ||
      http_buf_append_str(buf, buf_size, &pos, " HTTP/1.1\r\nHost: ") != 0 ||
      http_buf_append_str(buf, buf_size, &pos, req->host) != 0) {
    return -1;
  }

  if (!http_is_default_port(req)) {
    if (http_buf_append_char(buf, buf_size, &pos, ':') != 0 ||
        http_buf_append_u32(buf, buf_size, &pos, req->port) != 0) {
      return -1;
    }
  }

  if (!http_request_has_header(req, "Connection") &&
      http_buf_append_str(buf, buf_size, &pos, "\r\nConnection: keep-alive") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "User-Agent") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) CapyBrowser/0.9 Chrome/124.0.0.0 Safari/537.36") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept-Language") &&
      http_buf_append_str(buf, buf_size, &pos, "\r\nAccept-Language: en-US,en;q=0.7") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept-Encoding") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nAccept-Encoding: gzip, deflate, identity") != 0) {
    return -1;
  }
  if (req->method == HTTP_GET &&
      !http_request_has_header(req, "Upgrade-Insecure-Requests") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nUpgrade-Insecure-Requests: 1") != 0) {
    return -1;
  }
  if (req->body && req->body_len > 0 && !http_request_has_header(req, "Content-Length")) {
    if (http_buf_append_str(buf, buf_size, &pos, "\r\nContent-Length: ") != 0 ||
        http_buf_append_u32(buf, buf_size, &pos, (uint32_t)req->body_len) != 0) {
      return -1;
    }
  }

  for (uint32_t i = 0; i < req->header_count; i++) {
    if (http_buf_append_str(buf, buf_size, &pos, "\r\n") != 0 ||
        http_buf_append_str(buf, buf_size, &pos, req->headers[i].name) != 0 ||
        http_buf_append_str(buf, buf_size, &pos, ": ") != 0 ||
        http_buf_append_str(buf, buf_size, &pos, req->headers[i].value) != 0) {
      return -1;
    }
  }

  if (http_buf_append_str(buf, buf_size, &pos, "\r\n\r\n") != 0) {
    return -1;
  }
  buf[pos] = '\0';
  return (int)pos;
}

static int http_parse_status_line(const char *line, int *status_code) {
  /* HTTP/1.x NNN ... */
  if (http_strncmp(line, "HTTP/1.", 7) != 0) return -1;
  const char *p = line + 7;
  while (*p && *p != ' ') p++;
  if (*p == ' ') p++;
  *status_code = 0;
  while (*p >= '0' && *p <= '9') { *status_code = *status_code * 10 + (*p - '0'); p++; }
  return 0;
}

struct http_transport {
  int socket_fd;
  int use_tls;
  struct tls_context *tls;
};

/* ------------------------------------------------------------------ */
/* Keep-alive connection pool (up to 4 cached connections)            */
/* ------------------------------------------------------------------ */

#define HTTP_POOL_MAX 4

struct http_pool_entry {
  char host[128];
  uint16_t port;
  uint8_t use_tls;
  uint8_t active;
  int socket_fd;
  struct tls_context *tls;
};

static struct http_pool_entry g_http_pool[HTTP_POOL_MAX];

static struct http_pool_entry *http_pool_find(const char *host,
                                               uint16_t port, int use_tls) {
  for (int i = 0; i < HTTP_POOL_MAX; i++) {
    struct http_pool_entry *e = &g_http_pool[i];
    if (!e->active) continue;
    if (e->port != port || e->use_tls != (uint8_t)use_tls) continue;
    if (http_streq_ci(e->host, host)) return e;
  }
  return NULL;
}

static void http_pool_remove(struct http_pool_entry *e) {
  if (!e || !e->active) return;
  if (e->tls) { tls_close(e->tls); tls_free(e->tls); e->tls = NULL; }
  if (e->socket_fd >= 0) { socket_close(e->socket_fd); e->socket_fd = -1; }
  e->active = 0;
}

static void http_pool_store(const char *host, uint16_t port, int use_tls,
                             int socket_fd, struct tls_context *tls) {
  /* Find empty slot, evicting LRU (first active) if full */
  struct http_pool_entry *slot = NULL;
  for (int i = 0; i < HTTP_POOL_MAX; i++) {
    if (!g_http_pool[i].active) { slot = &g_http_pool[i]; break; }
  }
  if (!slot) {
    /* Evict first slot */
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

static void http_transport_reset(struct http_transport *transport) {
  if (!transport) return;
  transport->socket_fd = -1;
  transport->use_tls = 0;
  transport->tls = NULL;
}

static int http_transport_connect(struct http_transport *transport,
                                  const struct http_request *req,
                                  uint32_t ip) {
  struct sockaddr_in addr;
  struct tls_config tls_config;
  if (!transport || !req) return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  http_transport_reset(transport);

  /* Try reusing a pooled connection */
  {
    struct http_pool_entry *pooled = http_pool_find(req->host, req->port,
                                                     req->use_tls);
    if (pooled) {
      transport->socket_fd = pooled->socket_fd;
      transport->use_tls   = pooled->use_tls;
      transport->tls       = pooled->tls;
      /* Mark slot as inactive without closing */
      pooled->socket_fd = -1;
      pooled->tls       = NULL;
      pooled->active    = 0;
      return 0; /* reuse succeeded */
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

static int http_transport_send(struct http_transport *transport,
                               const void *buf, size_t len) {
  if (!transport || !buf || len == 0) return 0;
  if (transport->use_tls) {
    return tls_send(transport->tls, buf, len);
  }
  return socket_send(transport->socket_fd, buf, len, 0);
}

static int http_transport_send_all(struct http_transport *transport,
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

static int http_transport_recv(struct http_transport *transport,
                               void *buf, size_t len) {
  if (!transport || !buf || len == 0) return -1;
  if (transport->use_tls) {
    return tls_recv(transport->tls, buf, len);
  }
  return socket_recv(transport->socket_fd, buf, len, 0);
}

static void http_transport_close(struct http_transport *transport) {
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

const char *http_find_header(const struct http_response *resp, const char *name) {
  if (!resp || !name) return NULL;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (http_streq_ci(resp->headers[i].name, name)) {
      return resp->headers[i].value;
    }
  }
  return NULL;
}

static void http_set_header_value(struct http_response *resp, const char *name,
                                  const char *value) {
  if (!resp || !name || !value) return;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (!http_streq_ci(resp->headers[i].name, name)) continue;
    http_strcpy(resp->headers[i].value, value, sizeof(resp->headers[i].value));
    return;
  }
}

int http_request(const struct http_request *req, struct http_response *resp) {
  uint32_t ip = 0;
  struct http_transport transport;
  char request_buf[4096];
  char *recv_buf = NULL;
  size_t recv_capacity = HTTP_RECV_BUF_SIZE;
  size_t total = 0;
  int header_done = 0;
  char *body_start = NULL;
  size_t header_end_offset = 0;
  size_t body_received = 0;
  int req_len;
  int result = 0;
  int buffer_exhausted = 0;

  http_set_ok();
  if (!req || !resp) return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  http_memset(resp, 0, sizeof(*resp));

  /* Resolve hostname */
  if (dns_cache_lookup(req->host, &ip) != 0) {
    if (net_stack_dns_resolve(req->host, 3000, &ip) != 0) return http_fail(HTTP_ERR_DNS);
    dns_cache_insert(req->host, ip, 300);
  }

  http_transport_reset(&transport);
  if (http_transport_connect(&transport, req, ip) != 0) {
    return -g_http_last_error;
  }

  /* Build and send request */
  req_len = http_build_request(req, request_buf, sizeof(request_buf));
  if (req_len <= 0) {
    result = http_fail(HTTP_ERR_SEND);
    goto cleanup;
  }

  if (http_transport_send_all(&transport, request_buf, (size_t)req_len) != 0) {
    result = -g_http_last_error;
    goto cleanup;
  }

  if (req->body && req->body_len > 0) {
    if (http_transport_send_all(&transport, req->body, req->body_len) != 0) {
      result = -g_http_last_error;
      goto cleanup;
    }
  }

  /* Receive response */
  recv_buf = (char *)kmalloc(recv_capacity);
  if (!recv_buf) {
    result = http_fail(HTTP_ERR_NO_MEMORY);
    goto cleanup;
  }

  while (1) {
    int r;
    if (total + 1 >= recv_capacity) {
      size_t new_capacity = recv_capacity;
      char *grown = http_grow_recv_buffer(recv_buf, recv_capacity, total + 2,
                                          &new_capacity);
      if (!grown) {
        result = http_fail(recv_capacity >= HTTP_MAX_RESPONSE_SIZE
                               ? HTTP_ERR_RESPONSE_TOO_LARGE
                               : HTTP_ERR_NO_MEMORY);
        goto cleanup;
      }
      recv_buf = grown;
      recv_capacity = new_capacity;
    }

    r = http_transport_recv(&transport, recv_buf + total, recv_capacity - 1 - total);
    if (r < 0) {
      result = http_fail(HTTP_ERR_RECV);
      goto cleanup;
    }
    if (r == 0) break;
    total += (size_t)r;
    recv_buf[total] = '\0';

    if (!header_done) {
      for (size_t i = 0; i + 3 < total; i++) {
        if (recv_buf[i] == '\r' && recv_buf[i+1] == '\n' &&
            recv_buf[i+2] == '\r' && recv_buf[i+3] == '\n') {
          header_done = 1;
          header_end_offset = i + 4;
          body_start = recv_buf + header_end_offset;
          if (http_parse_status_line(recv_buf, &resp->status_code) != 0) {
            result = http_fail(HTTP_ERR_RESPONSE_PARSE);
            goto cleanup;
          }
          http_store_headers(recv_buf, header_end_offset, resp);
          resp->content_length = 0;
          for (uint32_t hi = 0; hi < resp->header_count; hi++) {
            if (http_streq_ci(resp->headers[hi].name, "Content-Length")) {
              const char *p = resp->headers[hi].value;
              while (*p >= '0' && *p <= '9') {
                resp->content_length = resp->content_length * 10 + (size_t)(*p - '0');
                p++;
              }
              break;
            }
          }
          break;
        }
      }
    }

    if (header_done) {
      size_t body_received = total - header_end_offset;
      if (resp->content_length > 0 && body_received >= resp->content_length) break;
      if (resp->chunked && body_start &&
          http_chunked_complete((const uint8_t *)body_start, body_received)) break;
    }
  }

  if (!buffer_exhausted) {
    buffer_exhausted = (total + 1 >= recv_capacity &&
                        recv_capacity >= HTTP_MAX_RESPONSE_SIZE);
  }

  if (!header_done) {
    result = http_fail(buffer_exhausted ? HTTP_ERR_RESPONSE_TOO_LARGE
                                        : HTTP_ERR_RESPONSE_PARSE);
    goto cleanup;
  }

  body_received = total - header_end_offset;
  if (buffer_exhausted &&
      ((resp->content_length > 0 && body_received < resp->content_length) ||
       (resp->chunked && body_start &&
        !http_chunked_complete((const uint8_t *)body_start, body_received)) ||
       (!resp->chunked && resp->content_length == 0))) {
    result = http_fail(HTTP_ERR_RESPONSE_TOO_LARGE);
    goto cleanup;
  }

  if (body_received > 0 && body_start) {
    const char *content_encoding;
    resp->body = (uint8_t *)kmalloc(body_received + 1);
    if (!resp->body) {
      result = http_fail(HTTP_ERR_NO_MEMORY);
      goto cleanup;
    }
    http_memcpy(resp->body, body_start, body_received);
    resp->body[body_received] = '\0';
    resp->body_len = body_received;
    if (resp->chunked) {
      size_t decoded_len = resp->body_len;
      if (http_decode_chunked_body(resp->body, &decoded_len) != 0) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_PARSE);
        goto cleanup;
      }
      resp->body_len = decoded_len;
    }

    content_encoding = http_find_header(resp, "Content-Encoding");
    if (http_encoding_requires_decode(content_encoding)) {
      uint8_t *decoded_body = NULL;
      size_t decoded_len = 0;
      int decode_rc = http_encoding_decode_body(content_encoding,
                                                resp->body,
                                                resp->body_len,
                                                HTTP_MAX_RESPONSE_SIZE,
                                                &decoded_body,
                                                &decoded_len);
      if (decode_rc == HTTP_ENCODING_ERR_NO_MEMORY) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_NO_MEMORY);
        goto cleanup;
      }
      if (decode_rc == HTTP_ENCODING_ERR_TOO_LARGE) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_TOO_LARGE);
        goto cleanup;
      }
      if (decode_rc != HTTP_ENCODING_OK) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_ENCODING);
        goto cleanup;
      }
      kfree(resp->body);
      resp->body = decoded_body;
      resp->body_len = decoded_len;
      resp->content_length = decoded_len;
      http_set_header_value(resp, "Content-Encoding", "identity");
    }
  }

  http_set_ok();
cleanup:
  if (recv_buf) {
    kfree(recv_buf);
  }
  /* Return connection to pool if keep-alive was negotiated and request succeeded */
  if (result == 0 && transport.socket_fd >= 0) {
    const char *conn_hdr = http_find_header(resp, "Connection");
    int do_keepalive = (conn_hdr && http_contains_ci(conn_hdr, "keep-alive"));
    /* HTTP/1.1 defaults to keep-alive unless Connection: close is specified */
    if (!conn_hdr && resp->status_code >= 200 && resp->status_code < 400) {
      int close_explicit = 0;
      for (uint32_t hi = 0; hi < resp->header_count; hi++) {
        if (http_streq_ci(resp->headers[hi].name, "Connection") &&
            http_contains_ci(resp->headers[hi].value, "close")) {
          close_explicit = 1; break;
        }
      }
      if (!close_explicit) do_keepalive = 1;
    }
    if (do_keepalive) {
      http_pool_store(req->host, req->port, req->use_tls,
                      transport.socket_fd, transport.tls);
      /* Don't let http_transport_close close the socket */
      transport.socket_fd = -1;
      transport.tls = NULL;
    }
  }
  http_transport_close(&transport);
  return result;
}

/* Compose an absolute URL from a Location header value.
 * Handles three Location forms:
 *   1) "https://host/path"    absolute (scheme + authority)
 *   2) "//host/path"          protocol-relative (inherit scheme)
 *   3) "/path" / "path"       path-relative (inherit scheme + authority)
 * Returns 0 on success, -1 if the result would not fit `out_size`.
 */
static int http_resolve_location(const struct http_request *base,
                                 const char *location,
                                 char *out, size_t out_size) {
  size_t pos = 0;
  if (!base || !location || !out || out_size < 16) return -1;

  /* Absolute URL with scheme */
  if (http_strncmp(location, "http://", 7) == 0 ||
      http_strncmp(location, "https://", 8) == 0) {
    http_strcpy(out, location, out_size);
    return out[0] ? 0 : -1;
  }

  /* Protocol-relative: //host/path */
  if (location[0] == '/' && location[1] == '/') {
    if (http_buf_append_str(out, out_size, &pos,
                            base->use_tls ? "https:" : "http:") != 0) return -1;
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
    out[pos] = '\0';
    return 0;
  }

  /* Build "scheme://host[:port]" prefix */
  if (http_buf_append_str(out, out_size, &pos,
                          base->use_tls ? "https://" : "http://") != 0) return -1;
  if (http_buf_append_str(out, out_size, &pos, base->host) != 0) return -1;
  if (!http_is_default_port(base)) {
    if (http_buf_append_char(out, out_size, &pos, ':') != 0) return -1;
    if (http_buf_append_u32(out, out_size, &pos, base->port) != 0) return -1;
  }

  if (location[0] == '/') {
    /* Origin-relative: replace path. */
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
  } else {
    /* Document-relative: drop the last path segment of base, append location. */
    const char *base_path = base->path[0] ? base->path : "/";
    size_t base_len = http_strlen(base_path);
    size_t cut = base_len;
    while (cut > 0 && base_path[cut - 1] != '/') cut--;
    if (cut == 0) {
      if (http_buf_append_char(out, out_size, &pos, '/') != 0) return -1;
    } else {
      for (size_t i = 0; i < cut; i++) {
        if (http_buf_append_char(out, out_size, &pos, base_path[i]) != 0) return -1;
      }
    }
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
  }

  out[pos] = '\0';
  return 0;
}

static int http_status_is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 ||
         status == 307 || status == 308;
}

int http_get(const char *url, struct http_response *resp) {
  if (!url || !resp) return http_fail(HTTP_ERR_INVALID_ARGUMENT);

  struct http_request req;
  char next_url[HTTP_MAX_URL];
  const int max_hops = 5;

  http_memset(resp, 0, sizeof(*resp));
  http_strcpy(next_url, url, sizeof(next_url));

  for (int hop = 0; hop <= max_hops; hop++) {
    http_memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.timeout_ms = 10000;

    if (http_parse_url(next_url, req.host, HTTP_MAX_HOST, req.path, HTTP_MAX_PATH,
                       &req.port, &req.use_tls) != 0) {
      return -g_http_last_error;
    }

    int rc = http_request(&req, resp);
    if (rc != 0) return rc;

    if (!http_status_is_redirect(resp->status_code)) {
      return 0;
    }
    if (hop == max_hops) {
      http_response_free(resp);
      return http_fail(HTTP_ERR_REDIRECT_LIMIT);
    }

    const char *location = http_find_header(resp, "Location");
    if (!location || !location[0]) {
      /* Redirect status without Location — surface as-is, caller can decide. */
      return 0;
    }

    /* Snapshot Location into a small buffer so we can free `resp` (and its
     * headers, which `location` points into) before we touch `next_url`. */
    char loc_copy[HTTP_MAX_HEADER_VALUE];
    http_strcpy(loc_copy, location, sizeof(loc_copy));

    char resolved[HTTP_MAX_URL];
    if (http_resolve_location(&req, loc_copy, resolved, sizeof(resolved)) != 0) {
      http_response_free(resp);
      return http_fail(HTTP_ERR_BAD_REDIRECT);
    }

    http_response_free(resp);
    http_strcpy(next_url, resolved, sizeof(next_url));
  }

  return http_fail(HTTP_ERR_REDIRECT_LIMIT);
}

int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len) {
  struct http_response resp;
  if (http_get(url, &resp) != 0) return -1;
  if (resp.status_code != 200) {
    http_response_free(&resp);
    return -1;
  }

  size_t copy = resp.body_len;
  if (copy > buffer_size) copy = buffer_size;
  if (resp.body && copy > 0) http_memcpy(buffer, resp.body, copy);
  if (out_len) *out_len = copy;
  http_response_free(&resp);
  return 0;
}

void http_response_free(struct http_response *resp) {
  if (resp) {
    if (resp->body) {
      kfree(resp->body);
      resp->body = NULL;
    }
    resp->body_len = 0;
    resp->content_length = 0;
    resp->header_count = 0;
    resp->chunked = 0;
  }
}
