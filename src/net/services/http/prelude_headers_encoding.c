#include "internal/http_internal.h"

int g_http_last_error = HTTP_OK;

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

void http_store_headers(const char *headers, size_t len,
                        struct http_response *resp) {
  size_t pos = 0;
  if (!headers || !resp) return;
  resp->location[0] = '\0';
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
      size_t full_value_len = 0;
      size_t value_len = 0;
      while (value_start < line_end && headers[value_start] == ' ') value_start++;
      full_value_len = line_end - value_start;
      value_len = full_value_len;
      hdr = &resp->headers[resp->header_count++];
      if (name_len >= sizeof(hdr->name)) name_len = sizeof(hdr->name) - 1;
      if (value_len >= sizeof(hdr->value)) value_len = sizeof(hdr->value) - 1;
      /* Strip non-printable bytes from header name and value during
       * storage. RFC 7230 §3.2.6 forbids control characters in
       * field-content; some real servers emit them anyway and a
       * hostile one could ship `Content-Type: text/html\x1b[2J...`
       * or `Location: https://x.com/\x1b[2J...`. cmd_net_query
       * echoes both directly via shell_print -> vga_write -> serial,
       * so an unfiltered control byte would land in a terminal
       * emulator as an ANSI escape. Replace each control byte / DEL
       * with '?' so the header text remains readable and length-
       * stable for downstream parsers that already key on prefixes
       * (Content-Length digits, "chunked" substring, etc). */
      for (size_t i = 0; i < name_len; i++) {
        unsigned char nc = (unsigned char)headers[line_start + i];
        hdr->name[i] = (nc <= 0x20u || nc == 0x7Fu) ? '?' : (char)nc;
      }
      hdr->name[name_len] = '\0';
      for (size_t i = 0; i < value_len; i++) {
        unsigned char vc = (unsigned char)headers[value_start + i];
        hdr->value[i] = (vc < 0x20u || vc == 0x7Fu) ? '?' : (char)vc;
      }
      hdr->value[value_len] = '\0';
      if (http_streq_ci(hdr->name, "Transfer-Encoding") &&
          http_contains_ci(hdr->value, "chunked")) {
        resp->chunked = 1;
      }
      /* Capture the full Location header value into the dedicated
       * resp->location buffer (HTTP_MAX_URL chars). The generic
       * headers[].value array is intentionally capped at
       * HTTP_MAX_HEADER_VALUE (256), which is way smaller than a
       * GitHub Release / Azure SAS redirect target (~1700 bytes).
       * Without this dedicated copy the redirect handler in
       * http_get would consume a truncated URL, the next hop would
       * send a request with a half-signature/half-jwt, the upstream
       * would close the connection without responding, and the
       * caller would see HTTP_ERR_RECV (rc=-6 "response receive
       * failed") with no actionable diagnostic. */
      if (http_streq_ci(hdr->name, "Location") &&
          resp->location[0] == '\0') {
        size_t loc_len = full_value_len;
        if (loc_len >= sizeof(resp->location)) {
          loc_len = sizeof(resp->location) - 1u;
        }
        for (size_t i = 0; i < loc_len; i++) {
          unsigned char vc = (unsigned char)headers[value_start + i];
          resp->location[i] = (vc < 0x20u || vc == 0x7Fu) ? '?' : (char)vc;
        }
        resp->location[loc_len] = '\0';
      }
    }
    pos = line_end;
    if (pos + 1 < len && headers[pos] == '\r' && headers[pos + 1] == '\n') pos += 2;
  }
}

int http_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  ch = http_tolower(ch);
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  return -1;
}

int http_parse_chunk_size(const uint8_t *buf, size_t len,
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

int http_chunked_complete(const uint8_t *buf, size_t len) {
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

int http_decode_chunked_body(uint8_t *body, size_t *body_len) {
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

char *http_grow_recv_buffer(char *buf, size_t current_capacity,
                            size_t min_capacity, size_t *new_capacity_out) {
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
