/*
 * userland/lib/capylibc-net/capy_net_http.c (F4 secao c parte 5)
 *
 * Minimal HTTP/1.1 client on top of the libcapy-net TCP layer.
 * One-shot `capy_http_get(url, buf, cap, &resp)`:
 *
 *   1. parse the URL via `capy_url_parse`;
 *   2. reject HTTPS via libcapy-tls fail-closed adapter;
 *   3. resolve + connect via `capy_tcp_connect_host`;
 *   4. build a "GET path HTTP/1.1\r\nHost: host[:port]\r\n
 *      Connection: close\r\n\r\n" request and send_all it;
 *   5. read into a small (4 KB) ring buffer until we've seen
 *      the empty line terminating the headers;
 *   6. parse the status line and headers;
 *   7. drain the body into the caller's buffer up to
 *      `body_buf_cap`, marking `truncated=1` if more arrives.
 *
 * Limitations (intentional, see capy_net.h header for rationale):
 *   - no chunked encoding (returns CAPY_NET_EUNSUPPORTED);
 *   - no auto-redirect (caller loops on Location);
 *   - no POST/PUT/DELETE (GET only);
 *   - no keep-alive (Connection: close baked into request);
 *   - no compression (Accept-Encoding: identity is implicit).
 *
 * Buffer sizing:
 *   - HEADER_BUF_CAP = 4096 covers any reasonable response head;
 *     bigger heads (rare server-side bug) are rejected as
 *     CAPY_NET_EHTTP rather than silently truncated.
 *   - Body is streamed directly into the caller's buf; we never
 *     allocate.
 */

#include "capylibc-net/capy_net.h"
#include "capylibc-tls/capy_tls.h"
#include "capylibc/capylibc.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);
extern int capy_net_internal_https_fail_closed(
    const struct capy_url_parts *url);
extern capy_net_err_t capy_net_internal_tls_error_to_net(capy_tls_err_t err);

#define HTTP_HEAD_BUF_CAP    4096
#define HTTP_DRAIN_CHUNK     256

/* === string utils ============================================ */

static int http_streq_ci(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static int http_buf_putc(char *buf, size_t cap, size_t *pos, char c) {
  if (*pos + 1 >= cap) return -1;
  buf[(*pos)++] = c;
  return 0;
}

static int http_buf_puts(char *buf, size_t cap, size_t *pos, const char *s) {
  while (*s) {
    if (http_buf_putc(buf, cap, pos, *s++) != 0) return -1;
  }
  return 0;
}

static int http_buf_putu16(char *buf, size_t cap, size_t *pos, uint16_t v) {
  /* 5 digits max for uint16_t. */
  char tmp[6];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  else {
    while (v > 0 && n < 5) {
      tmp[n++] = (char)('0' + (v % 10));
      v /= 10;
    }
  }
  while (n > 0) {
    if (http_buf_putc(buf, cap, pos, tmp[--n]) != 0) return -1;
  }
  return 0;
}

static int http_is_raw_ctl_or_space(char c) {
  unsigned char uc = (unsigned char)c;
  return uc <= 0x20u || uc == 0x7fu;
}

static int http_pct_encoded_nul_at(const char *p) {
  if (p[0] != '%') return 0;
  if (p[1] == '\0' || p[2] == '\0') return 0;
  return p[1] == '0' && p[2] == '0';
}

static int http_host_char_safe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static int http_host_safe(const char *host) {
  if (!host || !host[0]) return 0;
  size_t label_len = 0;
  char first = '\0';
  char prev = '\0';
  while (*host) {
    char c = *host;
    if (http_is_raw_ctl_or_space(c) || !http_host_char_safe(c)) return 0;
    if (c == '.') {
      if (label_len == 0 || first == '-' || prev == '-') return 0;
      label_len = 0;
      first = '\0';
      prev = c;
      host++;
      continue;
    }
    if (label_len == 0) first = c;
    label_len++;
    if (label_len > 63) return 0;
    prev = c;
    host++;
  }
  if (label_len == 0 || first == '-' || prev == '-') return 0;
  return 1;
}

static int http_path_safe(const char *path) {
  if (!path) return 0;
  if (!path[0]) return 1;
  if (path[0] != '/') return 0;
  while (*path) {
    if (http_is_raw_ctl_or_space(*path) || *path == '#' ||
        *path == '\\' || http_pct_encoded_nul_at(path)) return 0;
    path++;
  }
  return 1;
}

/* === request builder ========================================= */

int capy_http_build_get_request(const char *host, uint16_t port,
                                 const char *path,
                                 char *buf, size_t buf_cap) {
  capy_net_internal_reset_error();
  if (!host || !path || !buf || buf_cap < 32) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  if (port == 0) {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  if (!http_host_safe(host) || !http_path_safe(path)) {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  size_t pos = 0;
  if (http_buf_puts(buf, buf_cap, &pos, "GET ") != 0 ||
      http_buf_puts(buf, buf_cap, &pos, path[0] ? path : "/") != 0 ||
      http_buf_puts(buf, buf_cap, &pos, " HTTP/1.1\r\nHost: ") != 0 ||
      http_buf_puts(buf, buf_cap, &pos, host) != 0) {
    capy_net_internal_set_error(CAPY_NET_EBUF);
    return -1;
  }
  /* Only emit ":port" when it differs from a scheme default. Both 80
   * (http) and 443 (https) are scheme-default ports, so a canonical
   * Host header omits them: RFC 7230 §5.4 recommends it, and in
   * practice many virtual-host / SNI setups key on the bare authority
   * ("example.com") and mis-route or 404 "example.com:443". The builder
   * is scheme-agnostic (it only sees the resolved port), so it treats
   * both defaults the same; explicit non-default ports (8080, 8443, ...)
   * are still emitted. Slice 5.5 (real userland HTTPS via the transport
   * seam) made the 443 case reachable, so omitting it now matters. */
  if (port != 80 && port != 443) {
    if (http_buf_putc(buf, buf_cap, &pos, ':') != 0 ||
        http_buf_putu16(buf, buf_cap, &pos, port) != 0) {
      capy_net_internal_set_error(CAPY_NET_EBUF);
      return -1;
    }
  }
  if (http_buf_puts(buf, buf_cap, &pos,
                    "\r\nUser-Agent: capylibc-net/0.1"
                    "\r\nAccept: */*"
                    "\r\nConnection: close\r\n\r\n") != 0) {
    capy_net_internal_set_error(CAPY_NET_EBUF);
    return -1;
  }
  return (int)pos;
}

/* === status line parser ====================================== */

int capy_http_parse_status_line(const char *buf, size_t len, int *out_status) {
  capy_net_internal_reset_error();
  if (!buf || !out_status) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  /* Expect "HTTP/1.X SSS REASON\r\n" where X in {0,1} and SSS is
   * a 3-digit status code. Reason is ignored. */
  const char prefix[] = "HTTP/1.";
  size_t prefix_len = sizeof(prefix) - 1;
  if (len < prefix_len + 5) {  /* "HTTP/1.x SSS" minimum */
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  for (size_t i = 0; i < prefix_len; i++) {
    if (buf[i] != prefix[i]) {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
  }
  /* Minor version: '0' or '1'. */
  if (buf[prefix_len] != '0' && buf[prefix_len] != '1') {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  if (buf[prefix_len + 1] != ' ') {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  /* Status code: 3 digits. */
  size_t i = prefix_len + 2;
  if (i + 3 > len) {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  for (int k = 0; k < 3; k++) {
    if (buf[i + k] < '0' || buf[i + k] > '9') {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
  }
  int code = (buf[i] - '0') * 100 + (buf[i + 1] - '0') * 10 + (buf[i + 2] - '0');
  if (code < 100 || code > 599) {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  i += 3;
  if (i >= len || buf[i] != ' ') {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  i++;
  while (i < len && buf[i] != '\n') {
    unsigned char ch = (unsigned char)buf[i];
    if (buf[i] == '\r') {
      if (i + 1 >= len || buf[i + 1] != '\n') {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      i++;
      break;
    }
    if ((ch < 0x20 && ch != '\t') || ch == 0x7F) {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    i++;
  }
  if (i >= len || buf[i] != '\n') {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  *out_status = code;
  return (int)(i + 1);  /* index after the LF */
}

/* === header parser =========================================== */

static int http_header_copy(char *dst, size_t cap, const char *src,
                             size_t src_len) {
  /* Trim leading and trailing whitespace (RFC 7230 OWS). */
  while (src_len > 0 && (*src == ' ' || *src == '\t')) {
    src++; src_len--;
  }
  while (src_len > 0 &&
         (src[src_len - 1] == ' ' || src[src_len - 1] == '\t' ||
          src[src_len - 1] == '\r')) {
    src_len--;
  }
  if (src_len + 1 > cap) src_len = cap - 1;
  for (size_t i = 0; i < src_len; i++) dst[i] = src[i];
  dst[src_len] = '\0';
  return 0;
}

static int http_header_name_char_safe(char c) {
  unsigned char uc = (unsigned char)c;
  if (uc <= 0x20u || uc == 0x7fu) return 0;
  switch (c) {
    case '(': case ')': case '<': case '>': case '@':
    case ',': case ';': case ':': case '\\': case '"':
    case '/': case '[': case ']': case '?': case '=':
    case '{': case '}':
      return 0;
    default:
      return 1;
  }
}

static int http_header_name_safe(const char *src, size_t len) {
  if (!src || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    if (!http_header_name_char_safe(src[i])) return 0;
  }
  return 1;
}

static int http_header_value_safe(const char *src, size_t len) {
  if (!src) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char uc = (unsigned char)src[i];
    if ((uc < 0x20u && src[i] != '\t') || uc == 0x7fu) return 0;
  }
  return 1;
}

int capy_http_parse_headers(const char *buf, size_t len,
                             struct capy_http_response *out) {
  capy_net_internal_reset_error();
  if (!buf || !out) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  out->header_count = 0;
  int saw_terminator = 0;
  size_t i = 0;
  while (i < len) {
    /* Find end of line. */
    size_t line_start = i;
    while (i < len && buf[i] != '\n') i++;
    if (i >= len) {
      out->header_count = 0;
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    size_t line_end = i;  /* index of '\n' */
    i++;                  /* advance past '\n' for next iteration */

    /* Strip trailing '\r' from line_end. */
    size_t real_end = line_end;
    if (real_end > line_start && buf[real_end - 1] == '\r') real_end--;

    /* Empty line ⇒ end of headers. */
    if (real_end == line_start) {
      saw_terminator = 1;
      break;
    }

    if (buf[line_start] == ' ' || buf[line_start] == '\t') {
      out->header_count = 0;
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }

    /* Find ':' separator. */
    size_t colon = line_start;
    while (colon < real_end && buf[colon] != ':') colon++;
    if (colon >= real_end) {
      out->header_count = 0;
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }

    if (!http_header_name_safe(buf + line_start, colon - line_start) ||
        !http_header_value_safe(buf + colon + 1, real_end - (colon + 1))) {
      out->header_count = 0;
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    if (out->header_count >= CAPY_HTTP_MAX_HEADERS) continue;

    struct capy_http_header *h = &out->headers[out->header_count];
    http_header_copy(h->name, sizeof(h->name),
                     buf + line_start, colon - line_start);
    http_header_copy(h->value, sizeof(h->value),
                     buf + colon + 1, real_end - (colon + 1));
    out->header_count++;
  }
  if (!saw_terminator) {
    out->header_count = 0;
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  return out->header_count;
}

/* === response_find_header ==================================== */

const char *capy_http_response_find_header(
    const struct capy_http_response *resp, const char *name) {
  if (!resp || !name) return NULL;
  for (int i = 0; i < resp->header_count; i++) {
    if (http_streq_ci(resp->headers[i].name, name)) {
      return resp->headers[i].value;
    }
  }
  return NULL;
}

/* === content_length helper =================================== */

static int http_parse_content_length(const char *value, size_t len,
                                     size_t *out) {
  size_t i = 0;
  size_t v = 0;
  if (!value || !out) return -1;
  while (i < len && (value[i] == ' ' || value[i] == '\t')) i++;
  if (i >= len || value[i] < '0' || value[i] > '9') return -1;
  while (i < len && value[i] >= '0' && value[i] <= '9') {
    size_t digit = (size_t)(value[i] - '0');
    if (v > ((size_t)-1 - digit) / 10u) return -1;
    v = v * 10u + digit;
    i++;
  }
  while (i < len && (value[i] == ' ' || value[i] == '\t')) i++;
  if (i != len) return -1;
  *out = v;
  return 0;
}

static int http_header_name_equals_ci(const char *src, size_t len,
                                      const char *name) {
  if (!src || !name) return 0;
  for (size_t i = 0; i < len; i++) {
    char a = src[i];
    char b = name[i];
    if (!b) return 0;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return name[len] == '\0';
}

static int http_resolve_content_length(const char *buf, size_t len,
                                       size_t *out, int *known) {
  int seen = 0;
  size_t expected = 0;
  if (!buf || !out || !known) return -1;
  size_t i = 0;
  while (i < len) {
    size_t line_start = i;
    while (i < len && buf[i] != '\n') i++;
    if (i >= len) break;
    size_t line_end = i;
    i++;
    size_t real_end = line_end;
    if (real_end > line_start && buf[real_end - 1] == '\r') real_end--;
    if (real_end == line_start) break;
    size_t colon = line_start;
    while (colon < real_end && buf[colon] != ':') colon++;
    if (colon >= real_end) continue;
    if (!http_header_name_equals_ci(buf + line_start, colon - line_start,
                                    "Content-Length")) continue;
    size_t value = 0;
    if (http_parse_content_length(buf + colon + 1,
                                  real_end - (colon + 1), &value) != 0) {
      return -1;
    }
    if (seen && value != expected) return -1;
    expected = value;
    seen = 1;
  }
  *out = seen ? expected : 0;
  *known = seen;
  return 0;
}

static int http_status_is_informational(int status_code) {
  return status_code >= 100 && status_code < 200;
}

static int http_status_has_no_body(int status_code) {
  return status_code == 204 || status_code == 304;
}


static int http_content_encoding_value_is_identity(const char *value,
                                                   size_t len) {
  int seen = 0;
  size_t pos = 0;
  if (!value) return 0;
  while (1) {
    while (pos < len && (value[pos] == ' ' || value[pos] == '\t')) pos++;
    if (pos >= len) return seen;
    if (value[pos] == ',') return 0;
    size_t start = pos;
    while (pos < len && value[pos] != ',') pos++;
    size_t end = pos;
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
      end--;
    }
    if (end == start) return 0;
    if (!http_header_name_equals_ci(value + start, end - start, "identity")) {
      return 0;
    }
    seen = 1;
    if (pos >= len) return seen;
    pos++;
    size_t look = pos;
    while (look < len && (value[look] == ' ' || value[look] == '\t')) look++;
    if (look >= len || value[look] == ',') return 0;
    pos = look;
  }
}

static int http_headers_have_unsupported_content_encoding(const char *buf,
                                                          size_t len) {
  if (!buf) return 0;
  size_t i = 0;
  while (i < len) {
    size_t line_start = i;
    while (i < len && buf[i] != '\n') i++;
    if (i >= len) break;
    size_t line_end = i;
    i++;
    size_t real_end = line_end;
    if (real_end > line_start && buf[real_end - 1] == '\r') real_end--;
    if (real_end == line_start) break;
    size_t colon = line_start;
    while (colon < real_end && buf[colon] != ':') colon++;
    if (colon >= real_end) continue;
    if (!http_header_name_equals_ci(buf + line_start, colon - line_start,
                                    "Content-Encoding")) continue;
    if (!http_content_encoding_value_is_identity(buf + colon + 1,
                                                 real_end - (colon + 1))) {
      return 1;
    }
  }
  return 0;
}

static int http_headers_have_transfer_encoding(const char *buf, size_t len) {
  if (!buf) return 0;
  size_t i = 0;
  while (i < len) {
    size_t line_start = i;
    while (i < len && buf[i] != '\n') i++;
    if (i >= len) break;
    size_t line_end = i;
    i++;
    size_t real_end = line_end;
    if (real_end > line_start && buf[real_end - 1] == '\r') real_end--;
    if (real_end == line_start) break;
    size_t colon = line_start;
    while (colon < real_end && buf[colon] != ':') colon++;
    if (colon >= real_end) continue;
    if (http_header_name_equals_ci(buf + line_start, colon - line_start,
                                   "Transfer-Encoding")) return 1;
  }
  return 0;
}

/* === transport seam (TCP plaintext vs userland TLS) ========== */

/* HTTP runs over a raw TCP fd; HTTPS runs over a libcapy-tls context
 * wrapping that same fd, but only when capy_tls_is_supported() (i.e. built
 * with CAPYOS_TLS_USERLAND_HANDSHAKE) — otherwise the https gate above
 * fails closed and we never get here with is_https set. The send/recv/close
 * helpers dispatch on `tls` so capy_http_get's request/response logic stays
 * transport-agnostic. The fd is always owned here and torn down by
 * capy_http_conn_close. */
struct capy_http_conn {
  int fd;
  struct capy_tls_context *tls;
};

static long capy_http_conn_send_all(struct capy_http_conn *c,
                                    const void *buf, size_t len) {
  if (c->tls) {
    /* capy_tls_send writes the whole buffer (br_sslio_write_all + flush)
     * or returns -1, matching capy_send_all's all-or-error contract. */
    int n = capy_tls_send(c->tls, buf, len);
    return n < 0 ? -1 : (long)n;
  }
  return capy_send_all(c->fd, buf, len);
}

static long capy_http_conn_recv(struct capy_http_conn *c,
                                void *buf, size_t cap) {
  if (c->tls) {
    /* capy_tls_recv mirrors capy_recv_all: available bytes, 0 on clean
     * peer close, -1 on error. It rejects cap == 0, so guard it. */
    if (cap == 0) return 0;
    return (long)capy_tls_recv(c->tls, buf, cap);
  }
  return capy_recv_all(c->fd, buf, cap);
}

static void capy_http_conn_close(struct capy_http_conn *c) {
  if (c->tls) {
    (void)capy_tls_close(c->tls);
    capy_tls_free(c->tls);
    c->tls = NULL;
  }
  if (c->fd >= 0) {
    (void)capy_close(c->fd);
    c->fd = -1;
  }
}

/* === capy_http_get =========================================== */

int capy_http_get(const char *url,
                  uint8_t *body_buf, size_t body_buf_cap,
                  struct capy_http_response *out) {
  capy_net_internal_reset_error();
  if (!url || !out) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  if (!body_buf && body_buf_cap > 0) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }

  /* Zero the response so partial reads never leak prior state. */
  out->status_code = 0;
  out->header_count = 0;
  out->body_len = 0;
  out->content_length = 0;
  out->truncated = 0;

  struct capy_url_parts u;
  if (capy_url_parse(url, &u) != 0) {
    /* parse() already set the error code. */
    return -1;
  }
  if (u.is_https && capy_net_internal_https_fail_closed(&u) != 0) {
    return -1;
  }

  struct capy_http_conn conn;
  conn.fd = capy_tcp_connect_host(u.host, u.port);
  conn.tls = NULL;
  if (conn.fd < 0) {
    /* connect_host already set the error (EDNS / ESOCK / ECONNECT). */
    return -1;
  }
  if (u.is_https) {
    /* Reached only when capy_tls_is_supported() returned 1 (the https gate
     * above failed closed otherwise). Wrap the connected socket in the real
     * userland TLS handshake; from here send/recv flow through the engine. */
    struct capy_tls_config cfg;
    cfg.verify_peer = 1;
    cfg.ca_cert = NULL;
    cfg.ca_cert_len = 0;
    cfg.timeout_ms = CAPY_TLS_TIMEOUT_DEFAULT_MS;
    conn.tls = capy_tls_connect_tcp(conn.fd, u.host, &cfg);
    if (!conn.tls) {
      /* Handshake / cert validation failed: map the libcapy-tls error and
       * fail closed. capy_tls_connect_tcp does not own the fd, so close it. */
      capy_net_internal_set_error(
          capy_net_internal_tls_error_to_net(capy_tls_last_error()));
      (void)capy_close(conn.fd);
      return -1;
    }
  }

  /* Build the request. 1 KB is enough for a GET line + Host header
   * + a couple of standard headers; we cap at 1 KB to avoid
   * accidentally building a huge stack frame. */
  char request_buf[1024];
  int req_len = capy_http_build_get_request(u.host, u.port, u.path,
                                              request_buf, sizeof(request_buf));
  if (req_len <= 0) {
    capy_http_conn_close(&conn);
    return -1;
  }

  if (capy_http_conn_send_all(&conn, request_buf, (size_t)req_len) !=
      (long)req_len) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_ESEND);
    return -1;
  }

  /* Read the head + start of body into a fixed buffer until we see
   * an empty-line terminator. 4 KB is enough for any realistic response head. */
  char head[HTTP_HEAD_BUF_CAP];
  size_t head_len = 0;
  size_t header_end = 0;  /* index of byte AFTER the head terminator */
  while (head_len < sizeof(head)) {
    long n = capy_http_conn_recv(&conn, head + head_len,
                                 sizeof(head) - head_len);
    if (n < 0) {
      capy_http_conn_close(&conn);
      capy_net_internal_set_error(CAPY_NET_ERECV);
      return -1;
    }
    if (n == 0) {
      /* Server closed before we got the full head. */
      capy_http_conn_close(&conn);
      capy_net_internal_set_error(CAPY_NET_EHTTP);
      return -1;
    }
    head_len += (size_t)n;
    /* Search for the empty-line terminator in the buffer we have. */
    if (head_len >= 2) {
      for (size_t i = 0; i + 1 < head_len; i++) {
        if (i + 3 < head_len && head[i] == '\r' && head[i + 1] == '\n' &&
            head[i + 2] == '\r' && head[i + 3] == '\n') {
          header_end = i + 4;
          goto have_head;
        }
        if (head[i] == '\n' && head[i + 1] == '\n') {
          header_end = i + 2;
          goto have_head;
        }
      }
    }
  }
  /* Filled the whole 4 KB without finding the head terminator. */
  capy_http_conn_close(&conn);
  capy_net_internal_set_error(CAPY_NET_EHTTP);
  return -1;

have_head:;
  /* Parse the status line. */
  int after_status = capy_http_parse_status_line(head, header_end,
                                                   &out->status_code);
  if (after_status < 0) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EHTTP);
    return -1;
  }
  if (http_status_is_informational(out->status_code)) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EHTTP);
    return -1;
  }
  /* Parse the headers from after_status up to the empty line. */
  /* `header_end` includes the trailing empty-line terminator. */
  size_t headers_len = (size_t)(header_end - (size_t)after_status);
  if (capy_http_parse_headers(head + after_status, headers_len, out) < 0) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EHTTP);
    return -1;
  }

  /* Reject unsupported Transfer-Encoding before Content-Length framing. */
  if (http_headers_have_transfer_encoding(head + after_status, headers_len)) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EUNSUPPORTED);
    return -1;
  }
  if (http_headers_have_unsupported_content_encoding(head + after_status,
                                                     headers_len)) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EUNSUPPORTED);
    return -1;
  }
  int content_length_known = 0;
  if (http_resolve_content_length(head + after_status, headers_len,
                                  &out->content_length,
                                  &content_length_known) != 0) {
    capy_http_conn_close(&conn);
    capy_net_internal_set_error(CAPY_NET_EHTTP);
    return -1;
  }
  if (http_status_has_no_body(out->status_code)) {
    if (content_length_known && out->content_length != 0) {
      capy_http_conn_close(&conn);
      capy_net_internal_set_error(CAPY_NET_EHTTP);
      return -1;
    }
    out->content_length = 0;
    content_length_known = 1;
  }

  size_t body_received = 0;

  /* Already-read body bytes (the tail of the head buffer). */
  size_t already = head_len - header_end;
  if (already > 0) {
    size_t consume = already;
    if (content_length_known && consume > out->content_length) {
      consume = out->content_length;
    }
    body_received += consume;
    size_t store = consume;
    if (body_buf_cap > 0) {
      if (store > body_buf_cap - out->body_len) {
        store = body_buf_cap - out->body_len;
      }
      for (size_t i = 0; i < store; i++) {
        body_buf[out->body_len + i] = (uint8_t)head[header_end + i];
      }
      out->body_len += store;
    } else {
      store = 0;
    }
    if (store < consume) out->truncated = 1;
  }

  /* Drain the rest of the body. We stop when:
   *   - we've received content_length bytes on the wire (if known); OR
   *   - the peer closes (recv returns 0).
   * `out->body_len` tracks bytes stored for the caller only; `body_received`
   * tracks body octets consumed from the wire, including discarded tail bytes. */
  uint8_t scratch[HTTP_DRAIN_CHUNK];
  while (1) {
    if (content_length_known && body_received >= out->content_length) {
      break;
    }
    uint8_t *dst;
    size_t cap;
    int into_body = (body_buf_cap > 0 && out->body_len < body_buf_cap);
    if (into_body) {
      dst = body_buf + out->body_len;
      cap = body_buf_cap - out->body_len;
    } else {
      dst = scratch;
      cap = sizeof(scratch);
    }
    if (content_length_known) {
      size_t remaining = out->content_length - body_received;
      if (cap > remaining) cap = remaining;
    }
    if (cap == 0) break;
    long n = capy_http_conn_recv(&conn, dst, cap);
    if (n < 0) {
      capy_http_conn_close(&conn);
      capy_net_internal_set_error(CAPY_NET_ERECV);
      return -1;
    }
    if (n == 0) {
      if (content_length_known && body_received < out->content_length) {
        capy_http_conn_close(&conn);
        capy_net_internal_set_error(CAPY_NET_EHTTP);
        return -1;
      }
      break;  /* clean EOF */
    }
    body_received += (size_t)n;
    if (into_body) {
      out->body_len += (size_t)n;
      if (content_length_known && body_received < out->content_length &&
          out->body_len == body_buf_cap) {
        out->truncated = 1;
      }
    } else {
      out->truncated = 1;
    }
  }

  capy_http_conn_close(&conn);
  return 0;
}
