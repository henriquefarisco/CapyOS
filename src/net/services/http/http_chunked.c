/*
 * src/net/services/http/http_chunked.c
 *
 * HTTP/1.1 chunked transfer-encoding parsing, split out of
 * prelude_headers_encoding.c (preventive separation) so this
 * security-sensitive logic is a pure, self-contained TU: chunk-size
 * parsing (hex + optional ";ext"), a completeness probe and the
 * in-place chunked-body decode, with no transport, allocation or
 * global-state dependency. That makes it directly host-testable
 * (tests/net/test_http_chunked.c) — chunk parsing is a classic request-
 * smuggling / buffer-overrun vector, so it deserves focused coverage.
 *
 * Declarations live in internal/http_internal.h; request_response.c
 * consumes these to dechunk streamed response bodies. Only the
 * header-inline helpers http_tolower/http_memcpy are used.
 */

#include "internal/http_internal.h"

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
      /* Reject a chunk size that would overflow size_t on the next nibble
       * shift. A crafted oversized chunk-size line (many hex digits) would
       * otherwise wrap `size`; chunk parsing is a classic buffer-overrun
       * vector, so fail closed before the value can mislead the bounds
       * checks in http_chunked_complete/http_decode_chunked_body. */
      if (size > (SIZE_MAX >> 4)) return -1;
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
    /* Overflow-safe form of `pos + chunk_size + 1 >= len` (pos <= len here):
     * the chunk data plus its trailing CRLF must fit, and a huge chunk_size
     * must not wrap the bound and let the buf[pos]/buf[pos+1] read below run
     * past the buffer. The `len - pos < 2` short-circuit keeps `- 2` safe. */
    if (len - pos < 2 || chunk_size > (len - pos) - 2) return 0;
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
    /* Overflow-safe form of `src + chunk_size > len` (src <= len here): a
     * huge chunk_size must not wrap and bypass this bound, or the memcpy
     * below would over-read the source / over-write the destination. */
    if (chunk_size > len - src) return -1;
    http_memcpy(body + dst, body + src, chunk_size);
    dst += chunk_size;
    src += chunk_size;
    if (src + 1 >= len || body[src] != '\r' || body[src + 1] != '\n') return -1;
    src += 2;
  }
  return -1;
}
