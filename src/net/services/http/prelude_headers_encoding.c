#include "internal/http_internal.h"

int g_http_last_error = HTTP_OK;

http_progress_fn g_http_progress_fn = NULL;
void *g_http_progress_ctx = NULL;

int http_last_error(void) {
  return g_http_last_error;
}

void http_set_progress_observer(http_progress_fn fn, void *ctx) {
  g_http_progress_fn = fn;
  g_http_progress_ctx = ctx;
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
