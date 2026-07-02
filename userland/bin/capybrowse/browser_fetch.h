/*
 * userland/bin/capybrowse/browser_fetch.h — CapyOS-side multi-fetch browser
 * fetch runtime (Etapa 7 / Slice 7.5 transport binding).
 *
 * Binds the pure, host-tested CapyOS fetch-policy suite (http_session = cache +
 * per-domain cookie jar, net/http_session.h) to the real ring-3 HTTP transport
 * (capy_http_get_with_headers, capylibc-net). This is the piece the single-shot
 * text app was missing: a PERSISTENT, MULTI-FETCH runtime whose http_session
 * survives across navigations, so a repeat visit to a cacheable URL is served
 * from the in-process cache WITHOUT a second network request (the observable
 * "cache accelerates the 2nd visit" of the Etapa 7 acceptance criteria), and so
 * cookies set by one response ride along on later same-site requests.
 *
 * The whole context is CALLER-OWNED (no globals): a ring-3 app declares one in
 * .bss. The transport is seam-injected so the orchestration is host-testable
 * without a network (browser_fetch_get_with_transport + a fake fetch fn); the
 * convenience browser_fetch_get wires the real capylibc-net transport.
 */
#ifndef CAPYOS_BROWSER_FETCH_H
#define CAPYOS_BROWSER_FETCH_H

#include <stddef.h>
#include <stdint.h>

#include "net/http.h"          /* struct http_request / http_response (kernel model) */
#include "net/http_cache.h"    /* http_cache_fetch_fn, enum http_cache_result */
#include "net/http_session.h"  /* struct http_session */
#include "net/http_fetch_policy.h" /* HTTPS-first nav + mixed-content gate */
#include "net/http_hsts.h"     /* Strict-Transport-Security store */

/* Absolute-URL scratch: scheme + Host (<=HTTP_MAX_HOST) + ":port" + path
 * (<=HTTP_MAX_PATH). */
#define BROWSER_FETCH_URL_MAX (HTTP_MAX_HOST + HTTP_MAX_PATH + 16u)

/* Per-fetch response body scratch the real transport writes into and that the
 * cache copies from. Matched to the cache's per-entry body cap so any body the
 * cache is willing to store also fits the fetch buffer. */
#define BROWSER_FETCH_BODY_MAX HTTP_CACHE_BODY_MAX

struct browser_fetch_ctx {
  struct http_session session;        /* persistent cache + cookie jar */
  struct http_hsts_store hsts;        /* Strict-Transport-Security store */
  uint32_t transport_calls;           /* REAL network fetches actually issued */
  uint8_t fetch_body[BROWSER_FETCH_BODY_MAX]; /* real-transport body scratch */
};

/* Reset the session (cache + jar), the HSTS store and the transport counter. */
void browser_fetch_init(struct browser_fetch_ctx *ctx);

/* Plan a top-level navigation to `url`: classify its scheme, consult the HSTS
 * store (a known HSTS host forces https), then apply the HTTPS-first navigation
 * policy. Writes the EFFECTIVE url to use into `eff` (the https-upgraded form
 * when upgrading; the original normalized form otherwise) and, when non-NULL,
 * sets *hsts_required to 1 if HSTS makes the upgrade MANDATORY (a downgrade to
 * http must then be refused, never silently retried over http). Returns the
 * decision: ALLOW (fetch eff as-is), UPGRADE (fetch the https eff), or BLOCK
 * (refuse: unsupported scheme / unparseable url). Pure + host-testable. */
enum http_fetch_decision browser_fetch_plan(struct browser_fetch_ctx *ctx,
                                            const char *url, long now,
                                            char *eff, size_t eff_cap,
                                            int *hsts_required);

/* Mixed-content gate for a sub-resource (image/style/...). `page_secure` is 1
 * when the embedding document was loaded over https. Returns 1 if `suburl` may
 * be fetched (per http_fetch_policy_subresource), 0 if it must be blocked (e.g.
 * an http image on an https page, or a file:/javascript: sub-resource). A
 * NULL/unparseable suburl is blocked (0). Pure + host-testable. */
int browser_fetch_subresource_allowed(int page_secure, const char *suburl);

/* Compose an absolute URL ("http(s)://host[:port]/path") from a kernel-model
 * http_request into `buf`. The scheme-default port (80 http / 443 https) is
 * omitted; other ports are emitted. Returns the length (>0) or -1 on a NULL
 * arg or buffer overflow. Pure + host-testable. */
int browser_fetch_build_url(const struct http_request *req, char *buf,
                            size_t cap);

/* Parse `url` (via capylibc-net capy_url_parse) into a fresh GET http_request
 * (host/path/port/use_tls; no headers, no body). Returns 0 on success, -1 on a
 * NULL arg or a URL the parser rejects. Pure + host-testable. */
int browser_fetch_fill_request(const char *url, struct http_request *req);

/* Fetch `url` through the session (attach cookies -> cache flow -> harvest
 * Set-Cookie) using the caller-injected transport `fetch`. `now` is the cache
 * clock (seconds). Returns an enum http_cache_result (>=0) or
 * HTTP_CACHE_RESULT_ERROR (-1) on a NULL/parse error. Host-testable with a fake
 * transport: a 2nd fetch of a still-fresh URL returns FRESH_SERVED without
 * calling `fetch`. */
int browser_fetch_get_with_transport(struct browser_fetch_ctx *ctx,
                                      const char *url,
                                      struct http_response *resp, long now,
                                      http_cache_fetch_fn fetch, void *fctx);

/* Same, but drives the REAL ring-3 transport (capy_http_get_with_headers over
 * the Etapa 5 TLS/socket path). The request's cache/cookie headers (Cookie,
 * If-None-Match, If-Modified-Since) are forwarded to the wire; the response
 * body lands in ctx->fetch_body. Not host-testable (needs syscalls/network);
 * exercised by the ring-3 multi-fetch smoke. */
int browser_fetch_get(struct browser_fetch_ctx *ctx, const char *url,
                      struct http_response *resp, long now);

#endif /* CAPYOS_BROWSER_FETCH_H */
