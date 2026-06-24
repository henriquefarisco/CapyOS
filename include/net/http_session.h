#ifndef NET_HTTP_SESSION_H
#define NET_HTTP_SESSION_H

#include <stdint.h>
#include <stddef.h>

#include "net/http.h"
#include "net/http_cache.h"
#include "net/http_cookies.h"

/*
 * HTTP fetch session = response cache + per-domain cookie jar, composed into the
 * single RFC-correct fetch flow a browser session uses (Etapa 7 / Slice 7.5).
 * It binds the two pure CapyOS net adapters (http_cache, http_cookies) WITHOUT
 * coupling them to each other: the session owns one of each and sequences them
 * around an injected transport.
 *
 * Per fetch:
 *   1. attach the jar's matching cookies as a `Cookie:` request header;
 *   2. run the RFC 7234 cache flow (http_cache_fetch): a FRESH hit is served
 *      with NO transport fetch, else revalidate/fetch via the injected fn;
 *   3. harvest `Set-Cookie` from a NETWORK response (miss/refetch) into the jar.
 *
 * Pure, deterministic (clock injected via `now`), fail-closed and freestanding:
 * the session is CALLER-PROVIDED (no kernel .bss until allocated) and the
 * transport is injected (http_cache_fetch_fn), so the whole flow is testable in
 * host with no network. This is the entry point a real fetch binding (kernel
 * http_request, or a userland transport) will call; the binding itself awaits a
 * multi-fetch/persistent browser runtime (the single-shot text app does not
 * benefit from a cache).
 */
struct http_session {
  struct http_cache cache;
  struct http_cookie_jar jar;
  uint32_t cookie_headers_attached; /* requests that gained a Cookie header */
};

/* Reset cache + jar + stats. */
void http_session_init(struct http_session *s);

/* Attach the jar's matching cookies for (req->host, req->path, req->use_tls) at
 * `now` as a single `Cookie:` request header. Returns 1 if a header was added
 * (cookies matched and there was header room), else 0. */
int http_session_attach_cookies(struct http_session *s, struct http_request *req,
                                long now);

/* Full cache+cookie fetch: attach cookies, run the cache flow with the injected
 * transport, and harvest Set-Cookie from a network response. `req` may gain
 * Cookie + conditional headers, so pass a fresh request per call. Returns an
 * enum http_cache_result value (HTTP_CACHE_RESULT_ERROR on a NULL/!fetch arg). */
int http_session_fetch(struct http_session *s, struct http_request *req,
                       struct http_response *resp, long now,
                       http_cache_fetch_fn fetch, void *ctx);

#endif /* NET_HTTP_SESSION_H */
