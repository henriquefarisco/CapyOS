#ifndef NET_HTTP_CACHE_H
#define NET_HTTP_CACHE_H

#include <stdint.h>
#include <stddef.h>

#include "net/http.h" /* struct http_request, struct http_response */

/*
 * Bounded HTTP response cache (RFC 7234 subset) for the CapyOS browser/fetch
 * path (Etapa 7 / Slice 7.5). Sibling of the kernel DNS cache (dns_cache.c):
 * a CapyOS-owned network service (cache/cookies are CapyOS adapters, not the
 * decoupled CapyBrowser core).
 *
 * Pure, deterministic, fail-closed and freestanding (host-testable): the clock
 * is INJECTED (`now`, seconds) rather than read from a timer, and the store is
 * CALLER-PROVIDED (`struct http_cache`) rather than a static global, so there is
 * no kernel .bss cost until a caller allocates one and so freshness math is
 * reproducible under `make test`. Operates on the kernel HTTP model
 * (struct http_request / struct http_response, net/http.h).
 *
 * Scope (conservative — never serves stale incorrectly):
 *   - method GET only; cacheable status subset (200/203/301/308/404);
 *   - honours `Cache-Control: no-store` (request or response) and `no-cache`;
 *   - `Vary`-bearing responses are NOT cached (avoids serving the wrong variant);
 *   - freshness from `Cache-Control: max-age` (priority) or `Expires` - `Date`;
 *   - age per RFC 7234 4.2.3 (apparent age from `Date` + `Age` + resident time);
 *   - validators (`ETag` / `Last-Modified`) drive conditional revalidation
 *     (`If-None-Match` / `If-Modified-Since`) and 304 refresh;
 *   - bounded store: fixed entries, per-entry body cap, LRU eviction; an
 *     over-cap body is simply not cached (fetch proceeds normally).
 */

#define HTTP_CACHE_MAX_ENTRIES 8u
#define HTTP_CACHE_BODY_MAX 65536u /* per-entry body cap; larger -> not cached */
#define HTTP_CACHE_KEY_MAX (HTTP_MAX_HOST + HTTP_MAX_PATH + 16u)
#define HTTP_CACHE_VALIDATOR_MAX 128u /* ETag / Last-Modified verbatim */

enum http_cache_status {
  HTTP_CACHE_MISS = 0,  /* no usable entry -> fetch normally */
  HTTP_CACHE_FRESH = 1, /* entry is fresh -> serve from cache */
  HTTP_CACHE_STALE = 2  /* entry present but must be revalidated */
};

struct http_cache_entry {
  int valid;
  char key[HTTP_CACHE_KEY_MAX]; /* "GET https://host:port/path" */
  int status_code;
  /* freshness metadata (seconds, cache clock domain) */
  long response_time;      /* `now` when stored */
  long date_value;         /* parsed `Date:` (or response_time if absent) */
  long age_value;          /* parsed `Age:` delta-seconds (>= 0) */
  long freshness_lifetime; /* max-age, else Expires-Date, else 0 */
  int no_cache;            /* response `Cache-Control: no-cache` -> revalidate */
  /* validators (verbatim header values, for conditional requests) */
  char etag[HTTP_CACHE_VALIDATOR_MAX];
  char last_modified[HTTP_CACHE_VALIDATOR_MAX];
  /* stored response headers + Location, for faithful serving from cache */
  struct http_header headers[HTTP_MAX_HEADERS];
  uint32_t header_count;
  char location[HTTP_MAX_URL];
  /* stored body */
  uint8_t body[HTTP_CACHE_BODY_MAX];
  size_t body_len;
  uint64_t lru; /* last-use counter (higher = more recent) */
};

struct http_cache {
  struct http_cache_entry entries[HTTP_CACHE_MAX_ENTRIES];
  uint64_t clock; /* monotonically increasing LRU tick */
  uint32_t hits;
  uint32_t misses;
  uint32_t stores;
  uint32_t evictions;
  uint32_t revalidations;
};

/* Reset the cache to empty (clears entries + stats). */
void http_cache_init(struct http_cache *c);

/* Parse an HTTP IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT") to epoch seconds
 * (UTC). Returns -1 on a NULL/empty/malformed value. Tolerant of the leading
 * weekday token; requires the "DD Mon YYYY HH:MM:SS" core. */
long http_cache_parse_date(const char *s);

/* RFC 7234 storability: 1 if (req, resp) MAY be cached, else 0. */
int http_cache_is_cacheable(const struct http_request *req,
                            const struct http_response *resp);

/* Freshness lifetime (seconds): `Cache-Control: max-age` (priority), else
 * `Expires` - `Date` (when both parse and Expires >= Date), else 0. */
long http_cache_freshness_lifetime(const struct http_response *resp);

/* Current age (seconds) of a stored entry at `now` (>= 0). */
long http_cache_entry_age(const struct http_cache_entry *e, long now);

/* Store (req, resp) when cacheable; LRU-evicts when full; an over-cap body is
 * skipped. `now` is the cache clock. Returns 1 if stored, 0 if not. */
int http_cache_store(struct http_cache *c, const struct http_request *req,
                     const struct http_response *resp, long now);

/* Look up `req` at `now`: sets *out to the matching entry and returns FRESH or
 * STALE on a hit, or MISS otherwise. Updates stats + LRU on a hit. */
enum http_cache_status http_cache_lookup(struct http_cache *c,
                                          const struct http_request *req,
                                          long now,
                                          struct http_cache_entry **out);

/* Append conditional-revalidation headers (`If-None-Match` from ETag and/or
 * `If-Modified-Since` from Last-Modified) for a stale entry into `req`. Returns
 * the number of headers added (0 if the entry has no validator or req is full). */
int http_cache_add_conditional_headers(const struct http_cache_entry *e,
                                        struct http_request *req);

/* Refresh a stored entry after a 304 Not Modified: re-evaluate freshness from
 * the 304's headers and stamp response_time = `now`. */
void http_cache_refresh_on_304(struct http_cache *c, struct http_cache_entry *e,
                               const struct http_response *resp_304, long now);

/* Transport fetch injected into the orchestrator: performs the real fetch of
 * `req` into `resp`, returning 0 on success or non-zero on a transport error.
 * Decouples the cache flow from the actual HTTP client (kernel http_request,
 * a userland transport, or a host fake), so the flow is deterministically
 * testable without a network. */
typedef int (*http_cache_fetch_fn)(const struct http_request *req,
                                    struct http_response *resp, void *ctx);

enum http_cache_result {
  HTTP_CACHE_RESULT_ERROR = -1,       /* NULL arg or transport error */
  HTTP_CACHE_RESULT_MISS_FETCHED = 0, /* not cached -> fetched (stored if able) */
  HTTP_CACHE_RESULT_FRESH_SERVED = 1, /* served from cache, NO fetch issued */
  HTTP_CACHE_RESULT_REVALIDATED = 2,  /* stale -> 304 -> served cached body */
  HTTP_CACHE_RESULT_REFETCHED = 3     /* stale -> 200 -> replaced */
};

/*
 * RFC 7234 fetch flow around the cache, the entry point a fetcher uses:
 *   - FRESH hit  -> serve from cache, NO transport fetch (the fast 2nd visit);
 *   - STALE hit  -> add conditional headers, fetch; 304 reuses the cached body,
 *                   200 replaces it;
 *   - MISS       -> fetch, then store when cacheable.
 * `fetch` performs the transport; `now` is the cache clock. On a served or
 * revalidated result, `resp` points into the cache entry (valid until the next
 * cache mutation). `req` may gain conditional headers, so pass a fresh request
 * per call. Returns an enum http_cache_result.
 */
int http_cache_fetch(struct http_cache *c, struct http_request *req,
                     struct http_response *resp, long now,
                     http_cache_fetch_fn fetch, void *ctx);

#endif /* NET_HTTP_CACHE_H */
