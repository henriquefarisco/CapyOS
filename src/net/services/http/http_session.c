/*
 * src/net/services/http/http_session.c — HTTP fetch session (response cache +
 * per-domain cookie jar) for the CapyOS browser/fetch path (Etapa 7 / Slice
 * 7.5). See include/net/http_session.h. Composes the two pure net adapters
 * (http_cache + http_cookies) around an injected transport; pure, deterministic,
 * fail-closed, caller-owned.
 */
#include "net/http_session.h"

#include <stddef.h>
#include <stdint.h>

static void add_header(struct http_request *req, const char *name,
                       const char *value) {
  uint32_t n = req->header_count;
  size_t i;
  if (n >= HTTP_MAX_HEADERS) return;
  for (i = 0; i + 1 < sizeof(req->headers[n].name) && name[i]; i++)
    req->headers[n].name[i] = name[i];
  req->headers[n].name[i] = '\0';
  for (i = 0; i + 1 < sizeof(req->headers[n].value) && value[i]; i++)
    req->headers[n].value[i] = value[i];
  req->headers[n].value[i] = '\0';
  req->header_count = n + 1;
}

void http_session_init(struct http_session *s) {
  if (!s) return;
  http_cache_init(&s->cache);
  http_cookie_jar_init(&s->jar);
  s->cookie_headers_attached = 0;
}

int http_session_attach_cookies(struct http_session *s, struct http_request *req,
                                long now) {
  char hdr[HTTP_COOKIE_HEADER_MAX];
  size_t len;
  if (!s || !req) return 0;
  len = http_cookie_jar_header(&s->jar, req->host, req->path, req->use_tls, now,
                               hdr, sizeof(hdr));
  if (len == 0) return 0;
  if (req->header_count >= HTTP_MAX_HEADERS) return 0;
  add_header(req, "Cookie", hdr);
  s->cookie_headers_attached++;
  return 1;
}

int http_session_fetch(struct http_session *s, struct http_request *req,
                       struct http_response *resp, long now,
                       http_cache_fetch_fn fetch, void *ctx) {
  int r;
  if (!s || !req || !resp || !fetch) return HTTP_CACHE_RESULT_ERROR;

  /* 1. attach stored cookies to the outgoing request. */
  http_session_attach_cookies(s, req, now);

  /* 2. RFC 7234 cache flow (serves a fresh hit without calling `fetch`). */
  r = http_cache_fetch(&s->cache, req, resp, now, fetch, ctx);

  /* 3. harvest Set-Cookie only from a genuine NETWORK response (a served or
   *    revalidated hit returns the cached body, whose Set-Cookie was already
   *    processed when it was first stored). */
  if (r == HTTP_CACHE_RESULT_MISS_FETCHED || r == HTTP_CACHE_RESULT_REFETCHED)
    http_cookie_jar_set_from_response(&s->jar, req->host, req->path, resp, now);

  return r;
}
