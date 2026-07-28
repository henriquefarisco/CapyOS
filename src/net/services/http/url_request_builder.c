#include "internal/http_internal.h"
#include "core/version.h"

int http_init(void) {
  http_set_ok();
  g_http_last_status_code = 0;
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

  /* Reject any control byte (0x00-0x1F), the space character (0x20)
   * and DEL (0x7F) anywhere in the URL. The motivating threat is
   * CRLF injection into the Host header: a URL whose authority
   * contains embedded `\r\nGET /evil HTTP/1.1\r\nHost: ...` would
   * otherwise be copied verbatim into req->host by the loop below
   * and then concatenated into the wire request by
   * http_build_request, producing a stacked second HTTP request
   * (request smuggling). Upstream callers — the capypkg manifest
   * parser and update_agent_manifest_payload_url_valid — already
   * strip these bytes, but
   * `shell/commands/network_query.c::cmd_net_query` passes argv[1]
   * verbatim to http_get, so the URL parser is the last defense
   * before bytes reach the wire. Space is rejected because it has
   * no role in a URL outside RFC 3986 percent-encoding. */
  for (size_t i = 0u; url[i]; ++i) {
    unsigned char c = (unsigned char)url[i];
    if (c <= 0x20u || c == 0x7Fu) {
      return http_fail(HTTP_ERR_INVALID_URL);
    }
  }

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
    uint32_t p = 0;
    url++;
    while (*url >= '0' && *url <= '9') {
      saw_digit = 1;
      p = p * 10u + (uint32_t)(*url - '0');
      /* Reject an out-of-range port instead of letting it wrap a uint16_t:
       * ":65590" would otherwise truncate to 54 and silently connect to a
       * different port than the URL named. Accumulate in a uint32_t and
       * fail closed as soon as it leaves the 16-bit port space (this also
       * bounds the accumulator, so a long digit run cannot overflow it). */
      if (p > 65535u) return http_fail(HTTP_ERR_INVALID_URL);
      url++;
    }
    if (!saw_digit) return http_fail(HTTP_ERR_INVALID_URL);
    if (p > 0u) *port = (uint16_t)p;
  }

  if (*url == '/') http_strcpy(path, url, path_len);
  else http_strcpy(path, "/", path_len);

  return 0;
}

/* Dotted-quad host fast path. Without this, `http_get` sent every host through
 * the DNS resolver, so a URL naming a bare IPv4 address (a LAN update server, a
 * self-hosted mirror, the SLIRP gateway used by the Etapa 8 A/B gate) failed
 * with HTTP_ERR_DNS even though no name resolution was needed. Emits the
 * canonical host-order form used by NET_IPV4_ADDR and net_dns_parse_first_a.
 * Strict: exactly four decimal octets, each 1..3 digits without leading zeros,
 * no trailing bytes. Returns 0 on success. */
int http_parse_ipv4_literal(const char *host, uint32_t *out_ip) {
  uint32_t address = 0u;
  size_t i = 0u;

  if (!host || !out_ip) {
    return -1;
  }
  for (int octet = 0; octet < 4; ++octet) {
    uint32_t value = 0u;
    size_t digits = 0u;
    if (octet > 0) {
      if (host[i] != '.') return -1;
      ++i;
    }
    while (host[i] >= '0' && host[i] <= '9') {
      if (digits >= 3u) return -1;
      value = value * 10u + (uint32_t)(host[i] - '0');
      ++digits;
      ++i;
    }
    if (digits == 0u || value > 255u) return -1;
    if (digits > 1u && host[i - digits] == '0') return -1;
    address = (address << 8) | value;
  }
  if (host[i] != '\0') {
    return -1;
  }
  *out_ip = address;
  return 0;
}

int http_build_request(const struct http_request *req, char *buf, size_t buf_size) {
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
      http_buf_append_str(buf, buf_size, &pos, "\r\nConnection: close") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "User-Agent") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nUser-Agent: CapyOS/" CAPYOS_VERSION_EXTENDED) != 0) {
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
                          "\r\nAccept-Encoding: identity") != 0) {
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

int http_request_wants_keepalive(const struct http_request *req) {
  int wants_keepalive = 0;
  if (!req) return 0;
  for (uint32_t i = 0; i < req->header_count; i++) {
    if (!http_streq_ci(req->headers[i].name, "Connection")) continue;
    /* A close token always wins, including malformed duplicate headers. */
    if (http_contains_ci(req->headers[i].value, "close")) return 0;
    if (http_contains_ci(req->headers[i].value, "keep-alive")) {
      wants_keepalive = 1;
    }
  }
  return wants_keepalive;
}

int http_connection_should_pool(const struct http_request *req,
                                const struct http_response *resp) {
  int response_keepalive = 0;
  if (!req || !resp || !http_request_wants_keepalive(req)) return 0;
  if (resp->status_code < 200 || resp->status_code >= 400) return 0;
  if (resp->connection_close) return 0;
  response_keepalive = resp->connection_keep_alive;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (!http_streq_ci(resp->headers[i].name, "Connection")) continue;
    if (http_contains_ci(resp->headers[i].value, "close")) return 0;
    if (http_contains_ci(resp->headers[i].value, "keep-alive")) {
      response_keepalive = 1;
    }
  }
  /* Pool conservatively: both peers must opt in explicitly.  In particular,
   * the builder's default `Connection: close` can never enter the pool. */
  return response_keepalive;
}

int http_parse_status_line(const char *line, int *status_code) {
  int digits = 0;
  if (http_strncmp(line, "HTTP/1.", 7) != 0) return -1;
  const char *p = line + 7;
  while (*p && *p != ' ') p++;
  if (*p == ' ') p++;
  *status_code = 0;
  while (*p >= '0' && *p <= '9') {
    *status_code = *status_code * 10 + (*p - '0');
    /* An HTTP status code is exactly three digits (100-999). Reject an
     * overlong digit run instead of letting the signed int overflow
     * (undefined behavior) on a hostile status line. Capping at 3 digits
     * keeps *status_code <= 9999, well within int range. */
    if (++digits > 3) return -1;
    p++;
  }
  return 0;
}

size_t http_parse_content_length(const char *value) {
  size_t v = 0;
  if (!value) return 0;
  /* Parse the leading run of decimal digits (the value is OWS-trimmed and
   * control-filtered by http_store_headers). Saturate to SIZE_MAX on
   * overflow instead of wrapping: a Content-Length that wrapped to a small
   * value would let the receive loop in http_request treat a partial body
   * as complete and desync a reused keep-alive connection (response
   * smuggling). Saturating instead trips the HTTP_MAX_RESPONSE_SIZE cap,
   * which fails closed. */
  while (*value >= '0' && *value <= '9') {
    size_t digit = (size_t)(*value - '0');
    if (v > (SIZE_MAX - digit) / 10u) return SIZE_MAX;
    v = v * 10u + digit;
    value++;
  }
  return v;
}
