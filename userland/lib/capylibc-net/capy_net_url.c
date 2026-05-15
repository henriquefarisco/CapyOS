/*
 * userland/lib/capylibc-net/capy_net_url.c (F4 secao c parte 5)
 *
 * URL parser for libcapy-net. Splits `http[s]://host[:port]/path`
 * into a `struct capy_url_parts`. Strict-by-design (rejects
 * userinfo, IPv6 literals, ambiguous authority bytes and ports > 65535)
 * so apps don't have to defend against URL-confusion CVEs themselves.
 *
 * NOT a full RFC 3986 parser. Specifically:
 *   - userinfo (`user:pass@host`) is rejected -- accepting it would
 *     enable phishing-style URLs (`https://goodsite@badsite/`),
 *     which is the exact class of bug that earned `userinfo` its
 *     well-deserved deprecation in WHATWG URL.
 *   - IPv6 literals (`[::1]`) are rejected -- the kernel net
 *     stack is IPv4-only today.
 *   - host labels must be non-empty, <=63 bytes, and not start/end
 *     with '-'; unsafe bytes are rejected to avoid authority confusion.
 *   - query strings are kept in `path`, but fragments are stripped
 *     because they are client-side state and must not be sent in
 *     the HTTP request target.
 *   - port must be 1..65535; 0 is a parse failure.
 *
 * The parser is allocation-free; everything goes into the caller's
 * `out` struct, which is fully zero-initialised before any field
 * is set so a partial parse never leaks state.
 */

#include "capylibc-net/capy_net.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);

static int url_starts_with(const char *s, const char *prefix) {
  size_t i = 0;
  while (prefix[i]) {
    if (s[i] != prefix[i]) return 0;
    i++;
  }
  return 1;
}

static void url_zero(struct capy_url_parts *p) {
  p->is_https = 0;
  p->port = 0;
  p->host[0] = '\0';
  p->path[0] = '\0';
}

static int url_copy(char *dst, size_t cap, const char *src, size_t len) {
  if (len + 1 > cap) return -1;
  for (size_t i = 0; i < len; i++) dst[i] = src[i];
  dst[len] = '\0';
  return 0;
}

static int url_is_raw_ctl_or_space(char c) {
  unsigned char uc = (unsigned char)c;
  return uc <= 0x20u || uc == 0x7fu;
}

static int url_pct_encoded_nul_at(const char *p) {
  if (p[0] != '%') return 0;
  if (p[1] == '\0' || p[2] == '\0') return 0;
  return p[1] == '0' && p[2] == '0';
}

static int url_request_target_char_safe(const char *p) {
  return !url_is_raw_ctl_or_space(*p) && *p != '\\' &&
         !url_pct_encoded_nul_at(p);
}

static int url_host_char_safe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static int url_host_labels_safe(const char *host, size_t len) {
  size_t label_len = 0;
  char first = '\0';
  char prev = '\0';
  for (size_t i = 0; i < len; i++) {
    char c = host[i];
    if (!url_host_char_safe(c)) return 0;
    if (c == '.') {
      if (label_len == 0 || first == '-' || prev == '-') return 0;
      label_len = 0;
      first = '\0';
      prev = c;
      continue;
    }
    if (label_len == 0) first = c;
    label_len++;
    if (label_len > 63) return 0;
    prev = c;
  }
  if (label_len == 0 || first == '-' || prev == '-') return 0;
  return 1;
}

int capy_url_parse(const char *url, struct capy_url_parts *out) {
  capy_net_internal_reset_error();
  if (!url || !out) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  url_zero(out);

  const char *p = url;
  if (url_starts_with(p, "https://")) {
    out->is_https = 1;
    out->port = 443;
    p += 8;
  } else if (url_starts_with(p, "http://")) {
    out->is_https = 0;
    out->port = 80;
    p += 7;
  } else {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }

  /* The host segment runs until the first ':' (port), '/' (path),
   * '?' (query) or end-of-string. Unsupported authority bytes fail;
   * empty host fails. */
  const char *host_start = p;
  while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') {
    if (url_is_raw_ctl_or_space(*p) || !url_host_char_safe(*p)) {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    p++;
  }
  size_t host_len = (size_t)(p - host_start);
  if (host_len == 0 || !url_host_labels_safe(host_start, host_len)) {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }
  if (url_copy(out->host, sizeof(out->host), host_start, host_len) != 0) {
    capy_net_internal_set_error(CAPY_NET_EBUF);
    return -1;
  }

  if (*p == ':') {
    p++;
    /* Decimal port. Reject empty, non-digits, > 65535, leading 0
     * (avoid the "1.2.3.0001"-style ambiguity that bit inet_aton). */
    if (*p < '0' || *p > '9') {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    uint32_t port = 0;
    while (*p >= '0' && *p <= '9') {
      port = port * 10 + (uint32_t)(*p - '0');
      if (port > 65535) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
    if (port == 0) {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    out->port = (uint16_t)port;
  }

  /* The remainder is the path plus optional query. Fragment data is
   * client-side only, so it is validated for raw controls but not copied into
   * the request target. */
  if (*p == '\0') {
    out->path[0] = '/';
    out->path[1] = '\0';
    return 0;
  }
  if (*p == '/') {
    const char *path_start = p;
    while (*p && *p != '#') {
      if (!url_request_target_char_safe(p)) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
    size_t path_len = (size_t)(p - path_start);
    while (*p) {
      if (url_is_raw_ctl_or_space(*p)) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
    if (url_copy(out->path, sizeof(out->path), path_start, path_len) != 0) {
      capy_net_internal_set_error(CAPY_NET_EBUF);
      return -1;
    }
    return 0;
  }
  if (*p == '#') {
    while (*p) {
      if (url_is_raw_ctl_or_space(*p)) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
    out->path[0] = '/';
    out->path[1] = '\0';
    return 0;
  }
  if (*p == '?') {
    out->path[0] = '/';
    size_t out_pos = 1;
    while (*p && *p != '#') {
      if (!url_request_target_char_safe(p)) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      if (out_pos + 1 >= sizeof(out->path)) {
        capy_net_internal_set_error(CAPY_NET_EBUF);
        return -1;
      }
      out->path[out_pos++] = *p++;
    }
    while (*p) {
      if (url_is_raw_ctl_or_space(*p)) {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
    out->path[out_pos] = '\0';
    return 0;
  }

  /* Should be unreachable, but be defensive. */
  capy_net_internal_set_error(CAPY_NET_EPARSE);
  return -1;
}
