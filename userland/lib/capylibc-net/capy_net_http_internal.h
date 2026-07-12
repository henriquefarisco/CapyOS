/*
 * userland/lib/capylibc-net/capy_net_http_internal.h
 *
 * Shared, freestanding HTTP string/safety helpers used by BOTH the request
 * builder (capy_net_http_request.c) and the response/get path
 * (capy_net_http.c). Kept as `static inline` in this internal header so each
 * translation unit gets its own copy with no cross-file linkage. Carved out
 * when capy_net_http.c was split during the Etapa 7 / Slice 7.5 transport
 * request-header work (capy_http_get_with_headers) to keep both translation
 * units under the 900-line source-layout ceiling.
 *
 * Pure predicates over NUL-terminated / (ptr,len) inputs:
 *   - http_streq_ci            case-insensitive ASCII equality
 *   - http_header_name_safe    HTTP token (RFC 7230) name validation
 *   - http_header_value_safe   CTL-free header value (tab allowed) validation
 *   - http_req_headers_valid   caller request-header array validation; also
 *     rejects reserved framing/routing names (http_req_header_is_reserved).
 *     This is the anti request-smuggling gate the transport-header API
 *     enforces before a request reaches the wire.
 *
 * The bounded streaming chunk decoder is declared here too. Its implementation
 * lives in capy_net_http_chunked.c so the security-sensitive framing state
 * machine stays independently reviewable and capy_net_http.c remains below the
 * source-layout ceiling.
 */
#ifndef CAPY_NET_HTTP_INTERNAL_H
#define CAPY_NET_HTTP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "capylibc-net/capy_net.h"

static inline int http_streq_ci(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static inline int http_header_name_char_safe(char c) {
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

static inline int http_header_name_safe(const char *src, size_t len) {
  if (!src || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    if (!http_header_name_char_safe(src[i])) return 0;
  }
  return 1;
}

static inline int http_header_value_safe(const char *src, size_t len) {
  if (!src) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char uc = (unsigned char)src[i];
    if ((uc < 0x20u && src[i] != '\t') || uc == 0x7fu) return 0;
  }
  return 1;
}

/* Framing/routing-critical request headers a caller must NOT supply: the
 * builder owns Host and Connection, and Content-Length/Transfer-Encoding frame
 * the message. A caller-supplied copy would enable request smuggling or
 * Host-routing confusion, so reject them (case-insensitive). */
static inline int http_req_header_is_reserved(const char *name) {
  return http_streq_ci(name, "Host") ||
         http_streq_ci(name, "Connection") ||
         http_streq_ci(name, "Content-Length") ||
         http_streq_ci(name, "Transfer-Encoding") ||
         http_streq_ci(name, "Accept-Encoding");
}

/* Strict streaming response framing. Only one exact `chunked` transfer coding
 * is supported. Chunk extension/trailer lines and the aggregate trailer block
 * are bounded independently, preventing an attacker from turning metadata into
 * unbounded work even when the decoded body is tiny. */
#define CAPY_HTTP_CHUNK_LINE_MAX 1024u
#define CAPY_HTTP_TRAILER_MAX_BYTES 4096u
#define CAPY_HTTP_TRAILER_MAX_FIELDS 32u

enum capy_http_chunk_state {
  CAPY_HTTP_CHUNK_SIZE = 0,
  CAPY_HTTP_CHUNK_SIZE_LF,
  CAPY_HTTP_CHUNK_DATA,
  CAPY_HTTP_CHUNK_DATA_CR,
  CAPY_HTTP_CHUNK_DATA_LF,
  CAPY_HTTP_CHUNK_TRAILER,
  CAPY_HTTP_CHUNK_TRAILER_LF,
  CAPY_HTTP_CHUNK_DONE
};

struct capy_http_chunk_decoder {
  uint8_t *out;
  size_t out_cap;
  size_t out_len;
  size_t decoded_len;
  size_t chunk_remaining;
  size_t line_len;
  size_t trailer_bytes;
  unsigned int trailer_fields;
  enum capy_http_chunk_state state;
  int truncated;
  char line[CAPY_HTTP_CHUNK_LINE_MAX];
};

/* Parse raw response headers. Returns 0 for no Transfer-Encoding, 1 for the
 * single supported exact `chunked` coding, and -1 for every other/duplicate
 * form. The caller separately rejects TE+Content-Length as ambiguous framing. */
int capy_http_resolve_transfer_encoding(const char *headers, size_t len);

void capy_http_chunk_decoder_init(struct capy_http_chunk_decoder *decoder,
                                  uint8_t *out, size_t out_cap);

/* Consume one arbitrary transport fragment. Returns 1 only when the complete
 * terminal chunk and bounded trailer block ended exactly at `len`, 0 when more
 * bytes are required, and -1 for malformed framing or bytes after completion. */
int capy_http_chunk_decoder_feed(struct capy_http_chunk_decoder *decoder,
                                 const uint8_t *data, size_t len);

/* Validate a caller's request-header array: NULL only when count==0; each name
 * a valid HTTP token (non-empty), each value CTL-free (so no embedded CRLF can
 * inject a header line), and no reserved framing/routing name. Returns 1 if the
 * whole array is safe to emit, 0 otherwise. */
static inline int http_req_headers_valid(const struct capy_http_header *hdrs,
                                         int count) {
  if (count < 0) return 0;
  if (count > 0 && !hdrs) return 0;
  for (int i = 0; i < count; i++) {
    const char *name = hdrs[i].name;
    const char *value = hdrs[i].value;
    size_t name_len = 0, value_len = 0;
    while (name[name_len]) name_len++;
    while (value[value_len]) value_len++;
    if (!http_header_name_safe(name, name_len)) return 0;
    if (!http_header_value_safe(value, value_len)) return 0;
    if (http_req_header_is_reserved(name)) return 0;
  }
  return 1;
}

#endif /* CAPY_NET_HTTP_INTERNAL_H */
