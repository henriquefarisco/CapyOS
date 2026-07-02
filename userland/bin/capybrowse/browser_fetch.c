/*
 * userland/bin/capybrowse/browser_fetch.c — see browser_fetch.h.
 *
 * Multi-fetch browser fetch runtime: the persistent http_session (cache +
 * cookie jar) driven over the real ring-3 transport (capy_http_get_with_headers).
 * Freestanding: no host libc, only bounded local string helpers.
 */

#include "browser_fetch.h"

#include "capylibc-net/capy_net.h" /* capy_url_parse, capy_http_get_with_headers */

#include <stddef.h>
#include <stdint.h>

/* === bounded string helpers (freestanding) =================== */

/* Copy NUL-terminated `src` into `dst` (capacity `cap`, incl. terminator).
 * Returns 1 if it fit, 0 (and writes nothing) if it would overflow — callers
 * skip an over-long header rather than emit a truncated one. */
static int bf_copy_field(char *dst, size_t cap, const char *src) {
  size_t i;
  if (cap == 0) return 0;
  for (i = 0; src[i]; i++) {
    if (i + 1 >= cap) return 0;
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return 1;
}

static int bf_streq_ci(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

/* Append NUL-terminated `s` into buf at *pos (bounded). Returns 0 / -1. */
static int bf_append(char *buf, size_t cap, size_t *pos, const char *s) {
  while (*s) {
    if (*pos + 1 >= cap) return -1;
    buf[(*pos)++] = *s++;
  }
  return 0;
}

static int bf_append_u16(char *buf, size_t cap, size_t *pos, uint16_t v) {
  char tmp[6];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  else
    while (v > 0 && n < 5) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n > 0) {
    if (*pos + 1 >= cap) return -1;
    buf[(*pos)++] = tmp[--n];
  }
  return 0;
}

/* === public API ============================================== */

/* Extract the scheme token (before ':') of a raw URL and classify it. A token
 * longer than the buffer, or a URL with no ':', is HTTP_SCHEME_OTHER. */
static enum http_url_scheme bf_url_scheme(const char *url) {
  char tok[16];
  size_t i = 0;
  if (!url) return HTTP_SCHEME_OTHER;
  while (url[i] && url[i] != ':' && url[i] != '/') {
    if (i + 1 >= sizeof(tok)) return HTTP_SCHEME_OTHER;
    tok[i] = url[i];
    i++;
  }
  if (url[i] != ':') return HTTP_SCHEME_OTHER; /* no scheme delimiter */
  tok[i] = '\0';
  return http_fetch_classify_scheme(tok);
}

/* Compose "scheme://host[:port]/path" into buf (bounded). The scheme-default
 * port (80 http / 443 https) is omitted; other ports are emitted. Returns the
 * length (>0) or -1 on overflow. */
static int bf_compose_url(char *buf, size_t cap, int is_https, const char *host,
                          uint16_t port, const char *path) {
  size_t pos = 0;
  if (!buf || !host || cap < 8) return -1;
  if (bf_append(buf, cap, &pos, is_https ? "https://" : "http://") != 0)
    return -1;
  if (bf_append(buf, cap, &pos, host) != 0) return -1;
  if (port != 0 && port != 80 && port != 443) {
    if (pos + 1 >= cap) return -1;
    buf[pos++] = ':';
    if (bf_append_u16(buf, cap, &pos, port) != 0) return -1;
  }
  if (bf_append(buf, cap, &pos, (path && path[0]) ? path : "/") != 0) return -1;
  buf[pos] = '\0';
  return (int)pos;
}

void browser_fetch_init(struct browser_fetch_ctx *ctx) {
  if (!ctx) return;
  http_session_init(&ctx->session);
  http_hsts_init(&ctx->hsts);
  ctx->transport_calls = 0;
}

int browser_fetch_build_url(const struct http_request *req, char *buf,
                            size_t cap) {
  if (!req) return -1;
  return bf_compose_url(buf, cap, req->use_tls, req->host, req->port, req->path);
}

enum http_fetch_decision browser_fetch_plan(struct browser_fetch_ctx *ctx,
                                            const char *url, long now,
                                            char *eff, size_t eff_cap,
                                            int *hsts_required) {
  struct capy_url_parts u;
  enum http_url_scheme scheme;
  enum http_fetch_decision dec;
  int force_https = 0;

  if (hsts_required) *hsts_required = 0;
  if (!ctx || !url || !eff || eff_cap < 8) return HTTP_FETCH_BLOCK;
  /* capy_url_parse only accepts http/https; any other top-level scheme (file:,
   * ftp:, ...) is rejected here and treated as BLOCK. */
  if (capy_url_parse(url, &u) != 0) return HTTP_FETCH_BLOCK;
  scheme = u.is_https ? HTTP_SCHEME_HTTPS : HTTP_SCHEME_HTTP;

  /* HSTS: a known host (or a parent with includeSubDomains) forces https and
   * makes the upgrade mandatory (no http fallback). */
  if (http_hsts_should_upgrade(&ctx->hsts, u.host, now)) {
    force_https = 1;
    if (hsts_required) *hsts_required = 1;
  }

  dec = http_fetch_policy_navigation(scheme); /* https->ALLOW, http->UPGRADE */
  if (force_https && scheme == HTTP_SCHEME_HTTP) dec = HTTP_FETCH_UPGRADE;
  if (dec == HTTP_FETCH_BLOCK) return HTTP_FETCH_BLOCK;

  if (dec == HTTP_FETCH_UPGRADE) {
    /* Swap to https; an http-default port (80) becomes the https default. */
    uint16_t p = (u.port == 80) ? 443 : u.port;
    if (bf_compose_url(eff, eff_cap, 1, u.host, p, u.path) < 0)
      return HTTP_FETCH_BLOCK;
    return HTTP_FETCH_UPGRADE;
  }
  if (bf_compose_url(eff, eff_cap, u.is_https, u.host, u.port, u.path) < 0)
    return HTTP_FETCH_BLOCK;
  return HTTP_FETCH_ALLOW;
}

int browser_fetch_subresource_allowed(int page_secure, const char *suburl) {
  enum http_url_scheme s;
  if (!suburl) return 0;
  s = bf_url_scheme(suburl);
  return http_fetch_policy_subresource(page_secure ? 1 : 0, s) ==
         HTTP_FETCH_ALLOW;
}

int browser_fetch_fill_request(const char *url, struct http_request *req) {
  struct capy_url_parts u;
  size_t i;
  if (!url || !req) return -1;
  if (capy_url_parse(url, &u) != 0) return -1;
  req->method = HTTP_GET;
  req->use_tls = u.is_https;
  req->port = u.port;
  req->header_count = 0;
  req->body = NULL;
  req->body_len = 0;
  req->timeout_ms = 0;
  for (i = 0; i + 1 < sizeof(req->host) && u.host[i]; i++) req->host[i] = u.host[i];
  req->host[i] = '\0';
  for (i = 0; i + 1 < sizeof(req->path) && u.path[i]; i++) req->path[i] = u.path[i];
  req->path[i] = '\0';
  return 0;
}

/* Real ring-3 transport: http_cache_fetch_fn that builds the URL, forwards the
 * cache/cookie request headers, calls capy_http_get_with_headers, and maps the
 * userland response back to the kernel http_response model. */
static int browser_fetch_real_transport(const struct http_request *req,
                                         struct http_response *resp,
                                         void *vctx) {
  struct browser_fetch_ctx *ctx = (struct browser_fetch_ctx *)vctx;
  char url[BROWSER_FETCH_URL_MAX];
  struct capy_http_header chs[CAPY_HTTP_MAX_HEADERS];
  struct capy_http_response cr;
  uint32_t i;
  int n = 0;

  if (!ctx) return -1;
  ctx->transport_calls++;
  if (browser_fetch_build_url(req, url, sizeof(url)) < 0) return -1;

  /* Forward the headers the session/cache attached (Cookie, If-None-Match,
   * If-Modified-Since). Skip any that do not fit the userland header struct
   * rather than truncate (a truncated Cookie/validator is worse than absent). */
  for (i = 0; i < req->header_count && n < CAPY_HTTP_MAX_HEADERS; i++) {
    if (!bf_copy_field(chs[n].name, sizeof(chs[n].name), req->headers[i].name))
      continue;
    if (!bf_copy_field(chs[n].value, sizeof(chs[n].value), req->headers[i].value))
      continue;
    n++;
  }

  if (capy_http_get_with_headers(url, chs, n, ctx->fetch_body,
                                 sizeof(ctx->fetch_body), &cr) != 0)
    return -1;

  resp->status_code = cr.status_code;
  resp->body = ctx->fetch_body;
  resp->body_len = cr.body_len;
  resp->content_length = cr.content_length;
  resp->chunked = 0;
  resp->header_count = 0;
  resp->location[0] = '\0';
  for (i = 0; i < (uint32_t)cr.header_count && resp->header_count < HTTP_MAX_HEADERS;
       i++) {
    struct http_header *dst = &resp->headers[resp->header_count];
    if (!bf_copy_field(dst->name, sizeof(dst->name), cr.headers[i].name))
      continue;
    if (!bf_copy_field(dst->value, sizeof(dst->value), cr.headers[i].value))
      continue;
    if (bf_streq_ci(dst->name, "Location"))
      (void)bf_copy_field(resp->location, sizeof(resp->location), dst->value);
    resp->header_count++;
  }
  return 0;
}

int browser_fetch_get_with_transport(struct browser_fetch_ctx *ctx,
                                      const char *url,
                                      struct http_response *resp, long now,
                                      http_cache_fetch_fn fetch, void *fctx) {
  struct http_request req;
  int r;
  uint32_t i;
  if (!ctx || !url || !resp || !fetch) return HTTP_CACHE_RESULT_ERROR;
  if (browser_fetch_fill_request(url, &req) != 0) return HTTP_CACHE_RESULT_ERROR;
  r = http_session_fetch(&ctx->session, &req, resp, now, fetch, fctx);
  /* Harvest Strict-Transport-Security: a valid header received over a secure
   * connection registers the host so a later http navigation is forced to https
   * (http_hsts_process_header ignores it over http / without max-age). */
  if (r == HTTP_CACHE_RESULT_MISS_FETCHED || r == HTTP_CACHE_RESULT_REFETCHED ||
      r == HTTP_CACHE_RESULT_REVALIDATED) {
    for (i = 0; i < resp->header_count; i++) {
      if (bf_streq_ci(resp->headers[i].name, "Strict-Transport-Security")) {
        http_hsts_process_header(&ctx->hsts, req.host, resp->headers[i].value,
                                 req.use_tls, now);
        break;
      }
    }
  }
  return r;
}

int browser_fetch_get(struct browser_fetch_ctx *ctx, const char *url,
                      struct http_response *resp, long now) {
  return browser_fetch_get_with_transport(ctx, url, resp, now,
                                           browser_fetch_real_transport, ctx);
}
