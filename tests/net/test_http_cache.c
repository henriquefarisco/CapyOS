/*
 * tests/net/test_http_cache.c
 *
 * Host-side coverage for the bounded HTTP response cache (RFC 7234 subset,
 * src/net/services/http/http_cache.c). Pure policy + caller-owned store with an
 * INJECTED clock, so this test links only that TU and drives it by constructing
 * struct http_request / struct http_response in place. Locks the IMF-date
 * parser, cacheability rules, freshness/age math, store+lookup (fresh/stale),
 * conditional-revalidation headers, 304 refresh, LRU eviction and the
 * fail-closed paths.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "net/http_cache.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] http_cache: %s\n", (msg));                               \
    }                                                                          \
  } while (0)

/* ---- fixtures ----------------------------------------------------------- */

static void resp_reset(struct http_response *r, int status) {
  memset(r, 0, sizeof(*r));
  r->status_code = status;
  r->header_count = 0;
  r->body = NULL;
  r->body_len = 0;
}

static void resp_add(struct http_response *r, const char *name,
                     const char *value) {
  uint32_t i = r->header_count;
  if (i >= HTTP_MAX_HEADERS) return;
  strncpy(r->headers[i].name, name, sizeof(r->headers[i].name) - 1);
  strncpy(r->headers[i].value, value, sizeof(r->headers[i].value) - 1);
  r->header_count = i + 1;
}

static void get_req(struct http_request *q, const char *host, const char *path,
                    int tls) {
  memset(q, 0, sizeof(*q));
  q->method = HTTP_GET;
  strncpy(q->host, host, sizeof(q->host) - 1);
  strncpy(q->path, path, sizeof(q->path) - 1);
  q->use_tls = tls;
  q->port = tls ? 443 : 80;
  q->header_count = 0;
}

/* ---- tests -------------------------------------------------------------- */

static void test_parse_date(void) {
  /* The canonical RFC example: Sun, 06 Nov 1994 08:49:37 GMT == 784111777. */
  CHECK(http_cache_parse_date("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777L,
        "IMF-fixdate canonical epoch");
  /* Weekday optional. */
  CHECK(http_cache_parse_date("06 Nov 1994 08:49:37 GMT") == 784111777L,
        "date without weekday");
  /* Epoch zero. */
  CHECK(http_cache_parse_date("Thu, 01 Jan 1970 00:00:00 GMT") == 0L,
        "epoch zero");
  /* Malformed -> -1. */
  CHECK(http_cache_parse_date("not a date") == -1L, "garbage rejected");
  CHECK(http_cache_parse_date("") == -1L, "empty rejected");
  CHECK(http_cache_parse_date(NULL) == -1L, "NULL rejected");
  CHECK(http_cache_parse_date("Sun, 06 Zzz 1994 08:49:37 GMT") == -1L,
        "bad month rejected");
}

static void test_cacheability(void) {
  struct http_request q;
  struct http_response r;
  get_req(&q, "ex.com", "/", 1);

  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=3600");
  CHECK(http_cache_is_cacheable(&q, &r) == 1, "GET 200 max-age cacheable");

  resp_reset(&r, 200);
  resp_add(&r, "ETag", "\"abc\"");
  CHECK(http_cache_is_cacheable(&q, &r) == 1, "GET 200 ETag-only cacheable");

  resp_reset(&r, 200);
  CHECK(http_cache_is_cacheable(&q, &r) == 0,
        "GET 200 no freshness/validator NOT cacheable");

  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "no-store");
  CHECK(http_cache_is_cacheable(&q, &r) == 0, "no-store NOT cacheable");

  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=60");
  resp_add(&r, "Vary", "Accept-Encoding");
  CHECK(http_cache_is_cacheable(&q, &r) == 0, "Vary NOT cacheable");

  resp_reset(&r, 500);
  resp_add(&r, "Cache-Control", "max-age=60");
  CHECK(http_cache_is_cacheable(&q, &r) == 0, "500 NOT cacheable");

  /* request no-store also blocks. */
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=60");
  strncpy(q.headers[0].name, "Cache-Control", sizeof(q.headers[0].name) - 1);
  strncpy(q.headers[0].value, "no-store", sizeof(q.headers[0].value) - 1);
  q.header_count = 1;
  CHECK(http_cache_is_cacheable(&q, &r) == 0, "request no-store NOT cacheable");
  q.header_count = 0;

  /* POST never cacheable. */
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=60");
  q.method = HTTP_POST;
  CHECK(http_cache_is_cacheable(&q, &r) == 0, "POST NOT cacheable");
  q.method = HTTP_GET;
}

static void test_freshness_lifetime(void) {
  struct http_response r;
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "public, max-age=120");
  CHECK(http_cache_freshness_lifetime(&r) == 120L, "max-age lifetime");

  resp_reset(&r, 200);
  resp_add(&r, "Date", "Sun, 06 Nov 1994 08:49:37 GMT");
  resp_add(&r, "Expires", "Sun, 06 Nov 1994 09:49:37 GMT"); /* +3600 */
  CHECK(http_cache_freshness_lifetime(&r) == 3600L, "Expires-Date lifetime");

  resp_reset(&r, 200);
  resp_add(&r, "ETag", "\"x\"");
  CHECK(http_cache_freshness_lifetime(&r) == 0L, "no freshness signal -> 0");

  /* max-age wins over Expires. */
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=10");
  resp_add(&r, "Date", "Sun, 06 Nov 1994 08:49:37 GMT");
  resp_add(&r, "Expires", "Sun, 06 Nov 1994 09:49:37 GMT");
  CHECK(http_cache_freshness_lifetime(&r) == 10L, "max-age beats Expires");
}

static void test_store_lookup_fresh_stale(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r;
  struct http_cache_entry *e = NULL;
  static uint8_t body[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  http_cache_init(&c);
  get_req(&q, "ex.com", "/page", 1);
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=3600");
  r.body = body;
  r.body_len = sizeof(body);

  CHECK(http_cache_store(&c, &q, &r, 1000) == 1, "store cacheable response");
  CHECK(c.stores == 1, "store stat");

  CHECK(http_cache_lookup(&c, &q, 2000, &e) == HTTP_CACHE_FRESH && e != NULL,
        "fresh within max-age (age 1000 < 3600)");
  CHECK(e && e->body_len == 8 && e->body[0] == 1 && e->body[7] == 8,
        "stored body intact");
  CHECK(c.hits == 1, "hit stat");

  CHECK(http_cache_lookup(&c, &q, 5000, &e) == HTTP_CACHE_STALE,
        "stale past max-age (age 4000 > 3600)");
  CHECK(c.revalidations == 1, "revalidation stat");

  /* unknown key -> miss. */
  get_req(&q, "ex.com", "/other", 1);
  CHECK(http_cache_lookup(&c, &q, 2000, &e) == HTTP_CACHE_MISS && e == NULL,
        "unknown key miss");
  CHECK(c.misses == 1, "miss stat");
}

static void test_no_cache_forces_revalidation(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r;
  struct http_cache_entry *e = NULL;
  http_cache_init(&c);
  get_req(&q, "ex.com", "/nc", 1);
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "no-cache, max-age=3600");
  CHECK(http_cache_store(&c, &q, &r, 1000) == 1, "store no-cache entry");
  CHECK(http_cache_lookup(&c, &q, 1001, &e) == HTTP_CACHE_STALE,
        "no-cache always revalidates");
}

static void test_age_computation(void) {
  struct http_cache_entry e;
  memset(&e, 0, sizeof(e));
  /* stored at t=1000; Date said the response was generated at t=900; an
   * intermediary reported Age: 30. apparent=100, corrected=130; at now=1100
   * resident=100 -> age=230. */
  e.response_time = 1000;
  e.date_value = 900;
  e.age_value = 30;
  CHECK(http_cache_entry_age(&e, 1100) == 230L, "RFC age math");
  /* clock skew (now before stored) clamps resident to 0. */
  CHECK(http_cache_entry_age(&e, 500) == 130L, "negative resident clamped");
}

static void test_conditional_headers(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r;
  struct http_cache_entry *e = NULL;
  http_cache_init(&c);
  get_req(&q, "ex.com", "/v", 1);
  resp_reset(&r, 200);
  resp_add(&r, "ETag", "\"v1\"");
  resp_add(&r, "Last-Modified", "Sun, 06 Nov 1994 08:49:37 GMT");
  CHECK(http_cache_store(&c, &q, &r, 1000) == 1, "store validators-only entry");
  CHECK(http_cache_lookup(&c, &q, 1001, &e) == HTTP_CACHE_STALE && e,
        "validators-only entry is stale (lifetime 0)");

  {
    struct http_request rq;
    int added;
    get_req(&rq, "ex.com", "/v", 1);
    added = http_cache_add_conditional_headers(e, &rq);
    CHECK(added == 2, "two conditional headers added");
    CHECK(rq.header_count == 2, "request header_count grew");
    /* one must be If-None-Match=\"v1\". */
    {
      int found = 0;
      uint32_t i;
      for (i = 0; i < rq.header_count; i++) {
        if (strcmp(rq.headers[i].name, "If-None-Match") == 0 &&
            strcmp(rq.headers[i].value, "\"v1\"") == 0)
          found = 1;
      }
      CHECK(found, "If-None-Match carries the ETag");
    }
  }
}

static void test_refresh_on_304(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r, r304;
  struct http_cache_entry *e = NULL;
  http_cache_init(&c);
  get_req(&q, "ex.com", "/r", 1);
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=10");
  resp_add(&r, "ETag", "\"r1\"");
  CHECK(http_cache_store(&c, &q, &r, 1000) == 1, "store short-lived entry");
  CHECK(http_cache_lookup(&c, &q, 2000, &e) == HTTP_CACHE_STALE && e,
        "entry stale after max-age");

  /* server replies 304 with a fresh max-age. */
  resp_reset(&r304, 304);
  resp_add(&r304, "Cache-Control", "max-age=3600");
  http_cache_refresh_on_304(&c, e, &r304, 2000);
  CHECK(http_cache_lookup(&c, &q, 2500, &e) == HTTP_CACHE_FRESH,
        "entry fresh again after 304 refresh");
}

static void test_lru_eviction(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r;
  char path[32];
  unsigned k;
  http_cache_init(&c);
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=3600");
  /* insert MAX_ENTRIES+1 distinct keys; the first must be evicted. */
  for (k = 0; k < HTTP_CACHE_MAX_ENTRIES + 1; k++) {
    snprintf(path, sizeof(path), "/p%u", k);
    get_req(&q, "ex.com", path, 1);
    http_cache_store(&c, &q, &r, 1000 + (long)k);
  }
  CHECK(c.evictions == 1, "exactly one eviction at capacity+1");
  {
    struct http_cache_entry *e = NULL;
    get_req(&q, "ex.com", "/p0", 1); /* oldest -> evicted */
    CHECK(http_cache_lookup(&c, &q, 1001, &e) == HTTP_CACHE_MISS,
          "oldest entry evicted");
    get_req(&q, "ex.com", "/p8", 1); /* newest -> present */
    CHECK(http_cache_lookup(&c, &q, 1001, &e) == HTTP_CACHE_FRESH,
          "newest entry retained");
  }
}

static void test_oversize_and_fail_closed(void) {
  static struct http_cache c;
  struct http_request q;
  struct http_response r;
  static uint8_t big[HTTP_CACHE_BODY_MAX + 16];
  http_cache_init(&c);
  get_req(&q, "ex.com", "/big", 1);
  resp_reset(&r, 200);
  resp_add(&r, "Cache-Control", "max-age=3600");
  r.body = big;
  r.body_len = sizeof(big);
  CHECK(http_cache_store(&c, &q, &r, 1000) == 0, "over-cap body not stored");

  /* NULL args fail closed. */
  CHECK(http_cache_store(NULL, &q, &r, 1000) == 0, "NULL cache store fails");
  CHECK(http_cache_lookup(NULL, &q, 1000, NULL) == HTTP_CACHE_MISS,
        "NULL cache lookup miss");
  CHECK(http_cache_is_cacheable(NULL, &r) == 0, "NULL req not cacheable");
}

int run_http_cache_tests(void) {
  g_failures = 0;
  test_parse_date();
  test_cacheability();
  test_freshness_lifetime();
  test_store_lookup_fresh_stale();
  test_no_cache_forces_revalidation();
  test_age_computation();
  test_conditional_headers();
  test_refresh_on_304();
  test_lru_eviction();
  test_oversize_and_fail_closed();
  if (g_failures == 0) printf("[http_cache] all cache tests passed\n");
  return g_failures;
}
