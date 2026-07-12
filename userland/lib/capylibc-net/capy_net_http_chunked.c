/*
 * Strict, allocation-free HTTP/1.1 chunked response decoder for libcapy-net.
 *
 * The decoder is streaming: encoded bytes may be split at any boundary, while
 * only decoded bytes that fit the caller's body buffer are retained. Oversized
 * bodies are still drained and fully validated before success is returned.
 * Chunk extension lines, individual trailer lines, aggregate trailer bytes and
 * trailer field count all have independent hard limits.
 */

#include "capy_net_http_internal.h"

static int slice_eq_ci(const char *src, size_t len, const char *expected) {
  size_t i;
  if (!src || !expected) return 0;
  for (i = 0u; i < len; ++i) {
    char a = src[i];
    char b = expected[i];
    if (b == '\0') return 0;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return expected[len] == '\0';
}

int capy_http_resolve_transfer_encoding(const char *headers, size_t len) {
  size_t pos = 0u;
  int seen = 0;
  if (!headers) return -1;
  while (pos < len) {
    size_t start = pos;
    size_t end;
    size_t colon;
    size_t value_start;
    size_t value_end;
    while (pos < len && headers[pos] != '\n') pos++;
    if (pos >= len) return -1;
    end = pos++;
    if (end > start && headers[end - 1u] == '\r') end--;
    if (end == start) break;
    colon = start;
    while (colon < end && headers[colon] != ':') colon++;
    if (colon >= end) return -1;
    if (!slice_eq_ci(headers + start, colon - start, "Transfer-Encoding"))
      continue;
    if (seen) return -1;
    seen = 1;
    value_start = colon + 1u;
    while (value_start < end &&
           (headers[value_start] == ' ' || headers[value_start] == '\t'))
      value_start++;
    value_end = end;
    while (value_end > value_start &&
           (headers[value_end - 1u] == ' ' ||
            headers[value_end - 1u] == '\t'))
      value_end--;
    if (!slice_eq_ci(headers + value_start, value_end - value_start,
                     "chunked"))
      return -1;
  }
  return seen;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

static int parse_extension_value(const char *line, size_t len, size_t *pos) {
  size_t start;
  if (*pos >= len) return -1;
  if (line[*pos] == '"') {
    (*pos)++;
    while (*pos < len) {
      unsigned char c = (unsigned char)line[*pos];
      if (c == '"') {
        (*pos)++;
        return 0;
      }
      if (c == '\\') {
        (*pos)++;
        if (*pos >= len) return -1;
        c = (unsigned char)line[*pos];
        if (c < 0x20u || c == 0x7fu) return -1;
        (*pos)++;
        continue;
      }
      if ((c < 0x20u && c != '\t') || c == 0x7fu) return -1;
      (*pos)++;
    }
    return -1;
  }
  start = *pos;
  while (*pos < len && http_header_name_char_safe(line[*pos])) (*pos)++;
  return *pos > start ? 0 : -1;
}

static int parse_chunk_size_line(const char *line, size_t len,
                                 size_t *chunk_size) {
  size_t pos = 0u;
  size_t value = 0u;
  int digit;
  int saw_digit = 0;
  if (!line || !chunk_size || len == 0u) return -1;
  while (pos < len && (digit = hex_value(line[pos])) >= 0) {
    if (value > ((size_t)-1 >> 4u)) return -1;
    value = (value << 4u) | (size_t)digit;
    saw_digit = 1;
    pos++;
  }
  if (!saw_digit) return -1;
  while (pos < len) {
    size_t name_start;
    if (line[pos++] != ';') return -1;
    name_start = pos;
    while (pos < len && http_header_name_char_safe(line[pos])) pos++;
    if (pos == name_start) return -1;
    if (pos < len && line[pos] == '=') {
      pos++;
      if (parse_extension_value(line, len, &pos) != 0) return -1;
    }
  }
  *chunk_size = value;
  return 0;
}

static int trailer_name_forbidden(const char *name, size_t len) {
  return slice_eq_ci(name, len, "Content-Length") ||
         slice_eq_ci(name, len, "Transfer-Encoding") ||
         slice_eq_ci(name, len, "Host");
}

static int validate_trailer_line(const char *line, size_t len) {
  size_t colon = 0u;
  if (!line || len == 0u || line[0] == ' ' || line[0] == '\t') return -1;
  while (colon < len && line[colon] != ':') colon++;
  if (colon == 0u || colon >= len) return -1;
  if (!http_header_name_safe(line, colon) ||
      !http_header_value_safe(line + colon + 1u, len - colon - 1u) ||
      trailer_name_forbidden(line, colon))
    return -1;
  return 0;
}

void capy_http_chunk_decoder_init(struct capy_http_chunk_decoder *decoder,
                                  uint8_t *out, size_t out_cap) {
  if (!decoder) return;
  decoder->out = out;
  decoder->out_cap = out_cap;
  decoder->out_len = 0u;
  decoder->decoded_len = 0u;
  decoder->chunk_remaining = 0u;
  decoder->line_len = 0u;
  decoder->trailer_bytes = 0u;
  decoder->trailer_fields = 0u;
  decoder->state = CAPY_HTTP_CHUNK_SIZE;
  decoder->truncated = 0;
}

static int consume_data(struct capy_http_chunk_decoder *d,
                        const uint8_t *data, size_t len, size_t *pos) {
  size_t take = len - *pos;
  size_t room;
  size_t copy;
  size_t i;
  if (take > d->chunk_remaining) take = d->chunk_remaining;
  if (d->decoded_len > (size_t)-1 - take) return -1;
  room = d->out_len < d->out_cap ? d->out_cap - d->out_len : 0u;
  copy = take < room ? take : room;
  if (copy > 0u && !d->out) return -1;
  for (i = 0u; i < copy; ++i) d->out[d->out_len + i] = data[*pos + i];
  d->out_len += copy;
  d->decoded_len += take;
  d->chunk_remaining -= take;
  *pos += take;
  if (copy < take) d->truncated = 1;
  if (d->chunk_remaining == 0u) d->state = CAPY_HTTP_CHUNK_DATA_CR;
  return 0;
}

int capy_http_chunk_decoder_feed(struct capy_http_chunk_decoder *d,
                                 const uint8_t *data, size_t len) {
  size_t pos = 0u;
  if (!d || (!data && len != 0u) || (!d->out && d->out_cap != 0u)) return -1;
  if (d->state == CAPY_HTTP_CHUNK_DONE) return len == 0u ? 1 : -1;
  while (pos < len) {
    unsigned char c;
    if (d->state == CAPY_HTTP_CHUNK_DATA) {
      if (consume_data(d, data, len, &pos) != 0) return -1;
      continue;
    }
    c = data[pos++];
    switch (d->state) {
      case CAPY_HTTP_CHUNK_SIZE:
        if (c == '\r') {
          d->state = CAPY_HTTP_CHUNK_SIZE_LF;
        } else if (c == '\n' || d->line_len >= CAPY_HTTP_CHUNK_LINE_MAX) {
          return -1;
        } else {
          d->line[d->line_len++] = (char)c;
        }
        break;
      case CAPY_HTTP_CHUNK_SIZE_LF: {
        size_t chunk_size = 0u;
        if (c != '\n' ||
            parse_chunk_size_line(d->line, d->line_len, &chunk_size) != 0)
          return -1;
        d->line_len = 0u;
        d->chunk_remaining = chunk_size;
        d->state = chunk_size == 0u ? CAPY_HTTP_CHUNK_TRAILER
                                    : CAPY_HTTP_CHUNK_DATA;
        break;
      }
      case CAPY_HTTP_CHUNK_DATA_CR:
        if (c != '\r') return -1;
        d->state = CAPY_HTTP_CHUNK_DATA_LF;
        break;
      case CAPY_HTTP_CHUNK_DATA_LF:
        if (c != '\n') return -1;
        d->state = CAPY_HTTP_CHUNK_SIZE;
        break;
      case CAPY_HTTP_CHUNK_TRAILER:
        if (++d->trailer_bytes > CAPY_HTTP_TRAILER_MAX_BYTES) return -1;
        if (c == '\r') {
          d->state = CAPY_HTTP_CHUNK_TRAILER_LF;
        } else if (c == '\n' || d->line_len >= CAPY_HTTP_CHUNK_LINE_MAX) {
          return -1;
        } else {
          d->line[d->line_len++] = (char)c;
        }
        break;
      case CAPY_HTTP_CHUNK_TRAILER_LF:
        if (++d->trailer_bytes > CAPY_HTTP_TRAILER_MAX_BYTES || c != '\n')
          return -1;
        if (d->line_len == 0u) {
          d->state = CAPY_HTTP_CHUNK_DONE;
          if (pos != len) return -1;
          return 1;
        }
        if (validate_trailer_line(d->line, d->line_len) != 0 ||
            ++d->trailer_fields > CAPY_HTTP_TRAILER_MAX_FIELDS)
          return -1;
        d->line_len = 0u;
        d->state = CAPY_HTTP_CHUNK_TRAILER;
        break;
      case CAPY_HTTP_CHUNK_DONE:
        return -1;
      case CAPY_HTTP_CHUNK_DATA:
        break;
    }
  }
  return d->state == CAPY_HTTP_CHUNK_DONE ? 1 : 0;
}
