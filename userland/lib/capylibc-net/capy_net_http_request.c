/*
 * userland/lib/capylibc-net/capy_net_http_request.c
 *
 * HTTP/1.1 GET request-line + header builder for libcapy-net. Carved out of
 * capy_net_http.c during the Etapa 7 / Slice 7.5 transport request-header work
 * (capy_http_get_with_headers) so both translation units stay under the
 * 900-line source-layout ceiling.
 *
 * Owns:
 *   - capy_http_build_get_request_ex  builder with caller-supplied headers
 *                                     (Cookie / If-None-Match / If-Modified-Since
 *                                     from the browser cache/cookie layer);
 *   - capy_http_build_get_request     the no-extra-headers convenience wrapper.
 *
 * The host/path/port sanitisation and the bounded buffer writers live here as
 * file-local statics; the header name/value safety predicates and the caller
 * request-header validator are shared with the response/get path through
 * capy_net_http_internal.h.
 */

#include "capylibc-net/capy_net.h"
#include "capy_net_http_internal.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);

/* === bounded buffer writers ================================== */

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

/* === host / path sanitisation ================================ */

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

int capy_http_build_get_request_ex(const char *host, uint16_t port,
                                    const char *path,
                                    const struct capy_http_header *req_headers,
                                    int req_header_count,
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
  if (!http_req_headers_valid(req_headers, req_header_count)) {
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
                    "\r\nAccept: */*\r\n") != 0) {
    capy_net_internal_set_error(CAPY_NET_EBUF);
    return -1;
  }
  /* Caller-supplied request headers (validated above), each as
   * "Name: value\r\n". Emitted after the standard block and before the
   * framing Connection line so the cache/cookie layer's Cookie +
   * If-None-Match / If-Modified-Since reach the wire. */
  for (int i = 0; i < req_header_count; i++) {
    if (http_buf_puts(buf, buf_cap, &pos, req_headers[i].name) != 0 ||
        http_buf_puts(buf, buf_cap, &pos, ": ") != 0 ||
        http_buf_puts(buf, buf_cap, &pos, req_headers[i].value) != 0 ||
        http_buf_puts(buf, buf_cap, &pos, "\r\n") != 0) {
      capy_net_internal_set_error(CAPY_NET_EBUF);
      return -1;
    }
  }
  if (http_buf_puts(buf, buf_cap, &pos, "Connection: close\r\n\r\n") != 0) {
    capy_net_internal_set_error(CAPY_NET_EBUF);
    return -1;
  }
  return (int)pos;
}

int capy_http_build_get_request(const char *host, uint16_t port,
                                 const char *path,
                                 char *buf, size_t buf_cap) {
  return capy_http_build_get_request_ex(host, port, path, NULL, 0, buf, buf_cap);
}
