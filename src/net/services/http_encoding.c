#include "net/http_encoding.h"

#include "memory/kmem.h"

#include "tinf.h"

#include <stddef.h>

static void httpenc_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (len-- > 0) {
    *d++ = *s++;
  }
}

static char httpenc_tolower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

static int httpenc_is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int httpenc_token_eq_ci(const char *start, size_t len, const char *token) {
  size_t i = 0;
  if (!start || !token) return 0;
  while (token[i] && i < len) {
    if (httpenc_tolower(start[i]) != httpenc_tolower(token[i])) return 0;
    i++;
  }
  return token[i] == '\0' && i == len;
}

static size_t httpenc_parse_tokens(const char *encoding, char tokens[][16],
                                   size_t max_tokens) {
  size_t count = 0;
  size_t pos = 0;
  if (!encoding || !tokens || max_tokens == 0) return 0;
  while (encoding[pos] && count < max_tokens) {
    size_t start;
    size_t end;
    size_t len;
    size_t copy;
    while (encoding[pos] && (encoding[pos] == ',' || httpenc_is_space(encoding[pos]))) pos++;
    if (!encoding[pos]) break;
    start = pos;
    while (encoding[pos] && encoding[pos] != ',') pos++;
    end = pos;
    while (end > start && httpenc_is_space(encoding[end - 1])) end--;
    len = end - start;
    if (len == 0) continue;
    copy = len < 15 ? len : 15;
    for (size_t i = 0; i < copy; i++) {
      tokens[count][i] = httpenc_tolower(encoding[start + i]);
    }
    tokens[count][copy] = '\0';
    count++;
  }
  return count;
}

static uint32_t httpenc_read_le32(const uint8_t *buf) {
  return (uint32_t)buf[0] |
         ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

static size_t httpenc_clamp_capacity(size_t want, size_t max_output_size) {
  if (want < 256) want = 256;
  if (want > max_output_size) want = max_output_size;
  return want;
}

static size_t httpenc_guess_capacity(const char *token, const uint8_t *body,
                                     size_t body_len, size_t max_output_size) {
  size_t guess = body_len * 4;
  if (httpenc_token_eq_ci(token, 4, "gzip") && body_len >= 4) {
    guess = (size_t)httpenc_read_le32(body + body_len - 4);
  }
  if (guess < body_len) guess = body_len;
  return httpenc_clamp_capacity(guess, max_output_size);
}

static int httpenc_try_decode_once(const char *token,
                                   const uint8_t *body, size_t body_len,
                                   size_t max_output_size,
                                   uint8_t **decoded_body,
                                   size_t *decoded_len) {
  size_t capacity;
  if (!token || !body || !decoded_body || !decoded_len || max_output_size == 0) {
    return HTTP_ENCODING_ERR_INVALID_ARGUMENT;
  }
  if (body_len > 0xFFFFFFFFu || max_output_size > 0xFFFFFFFFu) {
    return HTTP_ENCODING_ERR_TOO_LARGE;
  }

  tinf_init();
  capacity = httpenc_guess_capacity(token, body, body_len, max_output_size);
  while (capacity > 0 && capacity <= max_output_size) {
    uint8_t *buffer = (uint8_t *)kmalloc(capacity + 1);
    unsigned int out_len = (unsigned int)capacity;
    int rc = TINF_DATA_ERROR;
    if (!buffer) return HTTP_ENCODING_ERR_NO_MEMORY;

    if (httpenc_token_eq_ci(token, 4, "gzip")) {
      rc = tinf_gzip_uncompress(buffer, &out_len, body, (unsigned int)body_len);
    } else if (httpenc_token_eq_ci(token, 7, "deflate")) {
      rc = tinf_zlib_uncompress(buffer, &out_len, body, (unsigned int)body_len);
      if (rc != TINF_OK) {
        out_len = (unsigned int)capacity;
        rc = tinf_uncompress(buffer, &out_len, body, (unsigned int)body_len);
      }
    } else {
      kfree(buffer);
      return HTTP_ENCODING_ERR_UNSUPPORTED;
    }

    if (rc == TINF_OK) {
      buffer[out_len] = '\0';
      *decoded_body = buffer;
      *decoded_len = (size_t)out_len;
      return HTTP_ENCODING_OK;
    }

    kfree(buffer);
    if (rc != TINF_BUF_ERROR) {
      return HTTP_ENCODING_ERR_DATA;
    }
    if (capacity == max_output_size) {
      return HTTP_ENCODING_ERR_TOO_LARGE;
    }
    capacity *= 2;
    if (capacity > max_output_size) capacity = max_output_size;
  }
  return HTTP_ENCODING_ERR_TOO_LARGE;
}

int http_encoding_requires_decode(const char *content_encoding) {
  char tokens[4][16];
  size_t count = httpenc_parse_tokens(content_encoding, tokens, 4);
  for (size_t i = 0; i < count; i++) {
    if (!httpenc_token_eq_ci(tokens[i], 8, "identity")) return 1;
  }
  return 0;
}

int http_encoding_decode_body(const char *content_encoding,
                              const uint8_t *body, size_t body_len,
                              size_t max_output_size,
                              uint8_t **decoded_body, size_t *decoded_len) {
  char tokens[4][16];
  size_t count;
  const uint8_t *current_body = body;
  size_t current_len = body_len;
  uint8_t *owned_current = NULL;

  if (!decoded_body || !decoded_len || (!body && body_len > 0)) {
    return HTTP_ENCODING_ERR_INVALID_ARGUMENT;
  }
  *decoded_body = NULL;
  *decoded_len = 0;
  if (!body) {
    return HTTP_ENCODING_OK;
  }

  count = httpenc_parse_tokens(content_encoding, tokens, 4);
  if (count == 0) {
    uint8_t *copy = (uint8_t *)kmalloc(body_len + 1);
    if (!copy) return HTTP_ENCODING_ERR_NO_MEMORY;
    httpenc_memcpy(copy, body, body_len);
    copy[body_len] = '\0';
    *decoded_body = copy;
    *decoded_len = body_len;
    return HTTP_ENCODING_OK;
  }

  for (size_t idx = count; idx > 0; idx--) {
    uint8_t *next_body = NULL;
    size_t next_len = 0;
    const char *token = tokens[idx - 1];
    int rc;
    if (httpenc_token_eq_ci(token, 8, "identity")) continue;
    rc = httpenc_try_decode_once(token, current_body, current_len,
                                 max_output_size, &next_body, &next_len);
    if (rc != HTTP_ENCODING_OK) {
      if (owned_current) kfree(owned_current);
      return rc;
    }
    if (owned_current) kfree(owned_current);
    owned_current = next_body;
    current_body = next_body;
    current_len = next_len;
  }

  if (!owned_current) {
    owned_current = (uint8_t *)kmalloc(body_len + 1);
    if (!owned_current) return HTTP_ENCODING_ERR_NO_MEMORY;
    httpenc_memcpy(owned_current, body, body_len);
    owned_current[body_len] = '\0';
    current_len = body_len;
  }

  *decoded_body = owned_current;
  *decoded_len = current_len;
  return HTTP_ENCODING_OK;
}
