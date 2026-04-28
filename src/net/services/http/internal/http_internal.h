#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include "net/http.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "net/stack.h"
#include "net/http_encoding.h"
#include "security/tls.h"
#include "memory/kmem.h"
#include <stddef.h>
#include <stdint.h>

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

#define HTTP_POOL_MAX 4

struct http_transport {
  int socket_fd;
  int use_tls;
  struct tls_context *tls;
};

struct http_pool_entry {
  char host[128];
  uint16_t port;
  uint8_t use_tls;
  uint8_t active;
  int socket_fd;
  struct tls_context *tls;
};

extern int g_http_last_error;

static inline void http_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static inline void http_memcpy(void *d, const void *s, size_t n) {
  uint8_t *dp = (uint8_t *)d;
  const uint8_t *sp = (const uint8_t *)s;
  for (size_t i = 0; i < n; i++) dp[i] = sp[i];
}
static inline size_t http_strlen(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
static inline char http_tolower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}
static inline void http_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}
static inline int http_buf_append_char(char *buf, size_t buf_size, size_t *pos, char ch) {
  if (!buf || !pos || *pos + 1 >= buf_size) return -1;
  buf[(*pos)++] = ch;
  return 0;
}
static inline int http_buf_append_str(char *buf, size_t buf_size, size_t *pos,
                                      const char *str) {
  if (!buf || !pos || !str) return -1;
  while (*str) {
    if (http_buf_append_char(buf, buf_size, pos, *str++) != 0) return -1;
  }
  return 0;
}
static inline int http_buf_append_u32(char *buf, size_t buf_size, size_t *pos,
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
static inline int http_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}
static inline int http_streq_ci(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] || b[i]) {
    if (http_tolower(a[i]) != http_tolower(b[i])) return 0;
    i++;
  }
  return 1;
}
static inline int http_contains_ci(const char *text, const char *needle) {
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
static inline int http_is_default_port(const struct http_request *req) {
  if (!req) return 1;
  return (req->use_tls && req->port == 443) || (!req->use_tls && req->port == 80);
}
static inline int http_request_has_header(const struct http_request *req,
                                           const char *name) {
  if (!req || !name) return 0;
  for (uint32_t i = 0; i < req->header_count; i++) {
    if (http_streq_ci(req->headers[i].name, name)) return 1;
  }
  return 0;
}
static inline void http_set_ok(void) { g_http_last_error = HTTP_OK; }
static inline int http_fail(int error) {
  g_http_last_error = error;
  return -error;
}

void http_store_headers(const char *headers, size_t len, struct http_response *resp);
int http_hex_digit(char ch);
int http_parse_chunk_size(const uint8_t *buf, size_t len,
                          size_t *header_len, size_t *chunk_size);
int http_chunked_complete(const uint8_t *buf, size_t len);
int http_decode_chunked_body(uint8_t *body, size_t *body_len);
char *http_grow_recv_buffer(char *buf, size_t current_capacity,
                            size_t min_capacity, size_t *new_capacity_out);

int http_build_request(const struct http_request *req, char *buf, size_t buf_size);
int http_parse_status_line(const char *line, int *status_code);

struct http_pool_entry *http_pool_find(const char *host, uint16_t port, int use_tls);
void http_pool_remove(struct http_pool_entry *e);
void http_pool_store(const char *host, uint16_t port, int use_tls,
                     int socket_fd, struct tls_context *tls);
void http_transport_reset(struct http_transport *transport);
int http_transport_connect(struct http_transport *transport,
                           const struct http_request *req, uint32_t ip);
int http_transport_send(struct http_transport *transport, const void *buf, size_t len);
int http_transport_send_all(struct http_transport *transport,
                            const void *buf, size_t len);
int http_transport_recv(struct http_transport *transport, void *buf, size_t len);
void http_transport_close(struct http_transport *transport);

void http_set_header_value(struct http_response *resp, const char *name,
                           const char *value);

int http_resolve_location(const struct http_request *base, const char *location,
                          char *out, size_t out_size);
int http_status_is_redirect(int status);

#endif
