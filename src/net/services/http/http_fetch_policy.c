/*
 * src/net/services/http/http_fetch_policy.c — fetch security policy (scheme
 * allow-list + HTTPS-first navigation + mixed-content blocking) for the CapyOS
 * browser/fetch path (Etapa 7 / Slice 7.6 hardening). See net/http_fetch_policy.h.
 * Pure, deterministic, fail-closed, freestanding (no libc, no state).
 */
#include "net/http_fetch_policy.h"

#include <stddef.h>

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Case-insensitive equality of NUL-terminated `s` with the lower-case literal
 * `lit`. */
static int ci_eq(const char *s, const char *lit) {
  size_t i = 0;
  for (i = 0; lit[i]; i++) {
    if (s[i] == '\0' || lc(s[i]) != lit[i]) return 0;
  }
  return s[i] == '\0';
}

enum http_url_scheme http_fetch_classify_scheme(const char *scheme) {
  if (!scheme || scheme[0] == '\0') return HTTP_SCHEME_OTHER;
  if (ci_eq(scheme, "https")) return HTTP_SCHEME_HTTPS;
  if (ci_eq(scheme, "http")) return HTTP_SCHEME_HTTP;
  if (ci_eq(scheme, "wss")) return HTTP_SCHEME_WSS;
  if (ci_eq(scheme, "ws")) return HTTP_SCHEME_WS;
  if (ci_eq(scheme, "data")) return HTTP_SCHEME_DATA;
  if (ci_eq(scheme, "blob")) return HTTP_SCHEME_BLOB;
  if (ci_eq(scheme, "about")) return HTTP_SCHEME_ABOUT;
  if (ci_eq(scheme, "file")) return HTTP_SCHEME_FILE;
  if (ci_eq(scheme, "ftp")) return HTTP_SCHEME_FTP;
  if (ci_eq(scheme, "javascript")) return HTTP_SCHEME_JAVASCRIPT;
  return HTTP_SCHEME_OTHER;
}

int http_fetch_scheme_is_secure(enum http_url_scheme s) {
  return s == HTTP_SCHEME_HTTPS || s == HTTP_SCHEME_WSS;
}

enum http_fetch_decision http_fetch_policy_navigation(enum http_url_scheme target) {
  switch (target) {
    case HTTP_SCHEME_HTTPS:
      return HTTP_FETCH_ALLOW;
    case HTTP_SCHEME_HTTP:
      return HTTP_FETCH_UPGRADE; /* HTTPS-first: try https before settling */
    case HTTP_SCHEME_DATA:
    case HTTP_SCHEME_ABOUT:
    case HTTP_SCHEME_BLOB:
      return HTTP_FETCH_ALLOW; /* inline / internal, no network transport */
    case HTTP_SCHEME_FILE:
    case HTTP_SCHEME_FTP:
    case HTTP_SCHEME_WS:
    case HTTP_SCHEME_WSS:
    case HTTP_SCHEME_JAVASCRIPT:
    case HTTP_SCHEME_OTHER:
    default:
      return HTTP_FETCH_BLOCK;
  }
}

enum http_fetch_decision http_fetch_policy_subresource(int page_secure,
                                                       enum http_url_scheme sub) {
  /* Never fetchable as a sub-resource by web content, regardless of page. */
  switch (sub) {
    case HTTP_SCHEME_FILE:
    case HTTP_SCHEME_FTP:
    case HTTP_SCHEME_JAVASCRIPT:
    case HTTP_SCHEME_OTHER:
      return HTTP_FETCH_BLOCK;
    case HTTP_SCHEME_DATA:
    case HTTP_SCHEME_BLOB:
    case HTTP_SCHEME_ABOUT:
      return HTTP_FETCH_ALLOW; /* inline / internal */
    default:
      break; /* http(s)/ws(s): subject to the mixed-content rule below */
  }
  if (page_secure) {
    /* A secure page may only pull secure sub-resources (block mixed content,
     * passive included -- the conservative modern default). */
    return http_fetch_scheme_is_secure(sub) ? HTTP_FETCH_ALLOW : HTTP_FETCH_BLOCK;
  }
  /* An insecure page may pull http or https sub-resources (no downgrade risk
   * to protect); ws/wss are not document sub-resources -> block. */
  if (sub == HTTP_SCHEME_HTTP || sub == HTTP_SCHEME_HTTPS) return HTTP_FETCH_ALLOW;
  return HTTP_FETCH_BLOCK;
}

const char *http_fetch_decision_name(enum http_fetch_decision d) {
  switch (d) {
    case HTTP_FETCH_BLOCK: return "BLOCK";
    case HTTP_FETCH_ALLOW: return "ALLOW";
    case HTTP_FETCH_UPGRADE: return "UPGRADE";
    default: return "?";
  }
}

const char *http_url_scheme_name(enum http_url_scheme s) {
  switch (s) {
    case HTTP_SCHEME_HTTPS: return "https";
    case HTTP_SCHEME_HTTP: return "http";
    case HTTP_SCHEME_WSS: return "wss";
    case HTTP_SCHEME_WS: return "ws";
    case HTTP_SCHEME_DATA: return "data";
    case HTTP_SCHEME_BLOB: return "blob";
    case HTTP_SCHEME_ABOUT: return "about";
    case HTTP_SCHEME_FILE: return "file";
    case HTTP_SCHEME_FTP: return "ftp";
    case HTTP_SCHEME_JAVASCRIPT: return "javascript";
    case HTTP_SCHEME_OTHER:
    default: return "other";
  }
}
