/*
 * tests/net/test_browser_fetch.c
 *
 * Host-side coverage for the multi-fetch browser fetch runtime
 * (userland/bin/capybrowse/browser_fetch.c). Exercises the pure pieces
 * (build_url, fill_request via capy_url_parse) and the persistent-session
 * orchestration with an INJECTED fake transport, proving the key Etapa 7
 * behaviour: a 2nd visit to a still-fresh cacheable URL is served from the
 * in-process cache WITHOUT a 2nd transport call. Network-free and deterministic
 * (clock injected). Links browser_fetch.c + http_session/cache/cookies +
 * capylibc-net (capy_url_parse), all already in the unit_tests suite.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "browser_fetch.h"
#include "page_budget.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] browser_fetch: %s\n", (msg));                            \
    }                                                                          \
  } while (0)

/* === build_url ============================================== */

static void mk_req(struct http_request *q, int tls, const char *host,
                   uint16_t port, const char *path) {
  memset(q, 0, sizeof(*q));
  q->method = HTTP_GET;
  q->use_tls = tls;
  q->port = port;
  strncpy(q->host, host, sizeof(q->host) - 1);
  strncpy(q->path, path, sizeof(q->path) - 1);
}

static void test_build_url(void) {
  struct http_request q;
  char buf[BROWSER_FETCH_URL_MAX];

  mk_req(&q, 0, "example.com", 80, "/");
  CHECK(browser_fetch_build_url(&q, buf, sizeof(buf)) > 0 &&
            strcmp(buf, "http://example.com/") == 0,
        "http default port omitted");

  mk_req(&q, 1, "example.com", 443, "/p?q=1");
  CHECK(browser_fetch_build_url(&q, buf, sizeof(buf)) > 0 &&
            strcmp(buf, "https://example.com/p?q=1") == 0,
        "https default port omitted, query preserved");

  mk_req(&q, 0, "x.example", 8080, "/api");
  CHECK(browser_fetch_build_url(&q, buf, sizeof(buf)) > 0 &&
            strcmp(buf, "http://x.example:8080/api") == 0,
        "non-default port emitted");

  mk_req(&q, 0, "example.com", 80, "/long/path/here");
  CHECK(browser_fetch_build_url(&q, buf, 12) == -1, "overflow -> -1");
  CHECK(browser_fetch_build_url(NULL, buf, sizeof(buf)) == -1, "NULL req -> -1");
}

/* === fill_request (capy_url_parse bridge) =================== */

static void test_fill_request(void) {
  struct http_request q;

  CHECK(browser_fetch_fill_request("https://example.com:8443/a/b?c=d", &q) == 0 &&
            q.use_tls == 1 && q.port == 8443 && q.method == HTTP_GET &&
            q.header_count == 0 && strcmp(q.host, "example.com") == 0 &&
            strcmp(q.path, "/a/b?c=d") == 0,
        "https URL with port + query parsed");

  CHECK(browser_fetch_fill_request("http://host.example/", &q) == 0 &&
            q.use_tls == 0 && q.port == 80 && strcmp(q.host, "host.example") == 0 &&
            strcmp(q.path, "/") == 0,
        "http URL default port");

  CHECK(browser_fetch_fill_request("ftp://nope/", &q) == -1, "non-http scheme -> -1");
  CHECK(browser_fetch_fill_request("garbage", &q) == -1, "garbage URL -> -1");
  CHECK(browser_fetch_fill_request(NULL, &q) == -1, "NULL url -> -1");
}

/* === multi-fetch cache short-circuit (the key proof) ======== */

struct fake {
  int calls;
  char seen_cookie[256];
  const char *cc;        /* Cache-Control to return */
  const char *set_cookie;
  const char *sts;       /* Strict-Transport-Security to return */
  const uint8_t *body;
  size_t body_len;
};

static int fake_fetch(const struct http_request *req, struct http_response *resp,
                      void *ctx) {
  struct fake *f = (struct fake *)ctx;
  uint32_t i;
  f->calls++;
  f->seen_cookie[0] = '\0';
  for (i = 0; i < req->header_count; i++)
    if (strcmp(req->headers[i].name, "Cookie") == 0)
      strncpy(f->seen_cookie, req->headers[i].value, sizeof(f->seen_cookie) - 1);
  memset(resp, 0, sizeof(*resp));
  resp->status_code = 200;
  if (f->cc) {
    strncpy(resp->headers[resp->header_count].name, "Cache-Control",
            sizeof(resp->headers[0].name) - 1);
    strncpy(resp->headers[resp->header_count].value, f->cc,
            sizeof(resp->headers[0].value) - 1);
    resp->header_count++;
  }
  if (f->set_cookie) {
    strncpy(resp->headers[resp->header_count].name, "Set-Cookie",
            sizeof(resp->headers[0].name) - 1);
    strncpy(resp->headers[resp->header_count].value, f->set_cookie,
            sizeof(resp->headers[0].value) - 1);
    resp->header_count++;
  }
  if (f->sts) {
    strncpy(resp->headers[resp->header_count].name, "Strict-Transport-Security",
            sizeof(resp->headers[0].name) - 1);
    strncpy(resp->headers[resp->header_count].value, f->sts,
            sizeof(resp->headers[0].value) - 1);
    resp->header_count++;
  }
  resp->body = (uint8_t *)f->body;
  resp->body_len = f->body_len;
  resp->content_length = f->body_len;
  return 0;
}

static void test_multifetch_cache_short_circuit(void) {
  static struct browser_fetch_ctx ctx; /* ~0.5 MiB: keep off the stack */
  struct http_response resp;
  struct fake f;
  static const uint8_t body[5] = {'h', 'e', 'l', 'l', 'o'};

  browser_fetch_init(&ctx);
  memset(&f, 0, sizeof(f));
  f.cc = "max-age=300";
  f.body = body;
  f.body_len = 5;

  /* 1st visit: MISS -> real transport called once, body cached. */
  CHECK(browser_fetch_get_with_transport(&ctx, "https://ex.com/page", &resp, 1000,
                                          fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED &&
            f.calls == 1 && resp.status_code == 200,
        "1st visit: miss -> fetched once");

  /* 2nd visit within max-age: served FROM CACHE, transport NOT called again. */
  CHECK(browser_fetch_get_with_transport(&ctx, "https://ex.com/page", &resp, 1010,
                                          fake_fetch, &f) ==
                HTTP_CACHE_RESULT_FRESH_SERVED &&
            f.calls == 1,
        "2nd visit: served from cache, NO refetch (cache accelerates 2nd visit)");
  CHECK(resp.status_code == 200 && resp.body_len == 5 &&
            memcmp(resp.body, "hello", 5) == 0,
        "cached body served faithfully");

  /* A different URL still misses -> a real fetch happens. */
  CHECK(browser_fetch_get_with_transport(&ctx, "https://ex.com/other", &resp, 1020,
                                          fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED &&
            f.calls == 2,
        "different URL misses and fetches");
}

static void test_cookie_rides_along(void) {
  static struct browser_fetch_ctx ctx;
  struct http_response resp;
  struct fake f;

  browser_fetch_init(&ctx);
  memset(&f, 0, sizeof(f));

  /* /login sets a cookie (uncacheable: no max-age). */
  f.set_cookie = "SID=abc";
  CHECK(browser_fetch_get_with_transport(&ctx, "https://ex.com/login", &resp, 2000,
                                          fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED,
        "login fetched");

  /* a later same-site request carries the stored cookie on the wire. */
  f.set_cookie = NULL;
  browser_fetch_get_with_transport(&ctx, "https://ex.com/app", &resp, 2001,
                                    fake_fetch, &f);
  CHECK(strcmp(f.seen_cookie, "SID=abc") == 0,
        "stored cookie attached to later request");
}

/* === navigation policy + HSTS (live fetch-policy wiring) ===== */

static void test_navigation_plan(void) {
  static struct browser_fetch_ctx ctx;
  char eff[BROWSER_FETCH_URL_MAX];
  int hsts_req = -1;
  browser_fetch_init(&ctx);

  CHECK(browser_fetch_plan(&ctx, "https://ex.com/p", 1000, eff, sizeof(eff),
                           &hsts_req) == HTTP_FETCH_ALLOW &&
            strcmp(eff, "https://ex.com/p") == 0 && hsts_req == 0,
        "https navigation -> ALLOW (normalized)");
  CHECK(browser_fetch_plan(&ctx, "http://ex.com/p", 1000, eff, sizeof(eff),
                           &hsts_req) == HTTP_FETCH_UPGRADE &&
            strcmp(eff, "https://ex.com/p") == 0 && hsts_req == 0,
        "http navigation -> UPGRADE to https (HTTPS-first)");
  CHECK(browser_fetch_plan(&ctx, "ftp://ex.com/", 1000, eff, sizeof(eff),
                           &hsts_req) == HTTP_FETCH_BLOCK,
        "non-http(s) navigation -> BLOCK");
  CHECK(browser_fetch_plan(NULL, "https://ex.com/", 1000, eff, sizeof(eff),
                           NULL) == HTTP_FETCH_BLOCK,
        "NULL ctx -> BLOCK");
}

static void test_hsts_forces_https(void) {
  static struct browser_fetch_ctx ctx;
  struct http_response resp;
  struct fake f;
  char eff[BROWSER_FETCH_URL_MAX];
  int hsts_req = 0;
  browser_fetch_init(&ctx);
  memset(&f, 0, sizeof(f));
  f.sts = "max-age=1000; includeSubDomains";

  /* a fetch over https registers the host's STS policy. */
  CHECK(browser_fetch_get_with_transport(&ctx, "https://secure.example/", &resp,
                                          5000, fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED,
        "https fetch carrying STS header");

  /* a later http navigation to that host is now MANDATORY https. */
  CHECK(browser_fetch_plan(&ctx, "http://secure.example/x", 5001, eff,
                           sizeof(eff), &hsts_req) == HTTP_FETCH_UPGRADE &&
            hsts_req == 1 && strcmp(eff, "https://secure.example/x") == 0,
        "HSTS forces https on later http navigation (mandatory)");
  CHECK(browser_fetch_plan(&ctx, "http://sub.secure.example/y", 5002, eff,
                           sizeof(eff), &hsts_req) == HTTP_FETCH_UPGRADE &&
            hsts_req == 1,
        "HSTS includeSubDomains covers a subdomain");
}

static void test_subresource_mixed_content(void) {
  CHECK(browser_fetch_subresource_allowed(1, "https://cdn.ex/img.png") == 1,
        "https sub-resource on https page allowed");
  CHECK(browser_fetch_subresource_allowed(1, "http://cdn.ex/img.png") == 0,
        "http sub-resource on https page blocked (mixed content)");
  CHECK(browser_fetch_subresource_allowed(0, "http://cdn.ex/img.png") == 1,
        "http sub-resource on http page allowed");
  CHECK(browser_fetch_subresource_allowed(1, "file:///etc/passwd") == 0,
        "file: sub-resource blocked");
  CHECK(browser_fetch_subresource_allowed(1, "javascript:alert(1)") == 0,
        "javascript: sub-resource blocked");
  CHECK(browser_fetch_subresource_allowed(1, NULL) == 0,
        "NULL sub-resource blocked");
}

/* === per-page memory/time budget ============================ */

static void test_page_budget(void) {
  struct page_budget b;

  page_budget_init(&b, 100, 0, 1000);
  CHECK(page_budget_add_bytes(&b, 60) == 1 && page_budget_ok(&b) == 1,
        "under byte cap ok");
  CHECK(page_budget_remaining_bytes(&b) == 40, "remaining bytes tracked");
  CHECK(page_budget_add_bytes(&b, 50) == 0 && page_budget_ok(&b) == 0,
        "over byte cap fails closed");
  CHECK(page_budget_add_bytes(&b, 1) == 0, "byte cap stays exceeded (sticky)");

  page_budget_init(&b, 0, 5, 1000);
  CHECK(page_budget_check_time(&b, 1003) == 1, "within time budget");
  CHECK(page_budget_check_time(&b, 1006) == 0 && page_budget_ok(&b) == 0,
        "over time budget fails closed");

  page_budget_init(&b, 0, 0, 0);
  CHECK(page_budget_add_bytes(&b, 1000000u) == 1 &&
            page_budget_remaining_bytes(&b) == (size_t)-1,
        "unlimited byte budget");

  CHECK(page_budget_add_bytes(NULL, 1) == 0 && page_budget_ok(NULL) == 0,
        "NULL budget fail-closed");
}

static void test_fail_closed(void) {
  static struct browser_fetch_ctx ctx;
  struct http_response resp;
  struct fake f;
  browser_fetch_init(&ctx);
  memset(&f, 0, sizeof(f));
  CHECK(browser_fetch_get_with_transport(NULL, "https://ex.com/", &resp, 1,
                                          fake_fetch, &f) ==
            HTTP_CACHE_RESULT_ERROR,
        "NULL ctx -> ERROR");
  CHECK(browser_fetch_get_with_transport(&ctx, NULL, &resp, 1, fake_fetch, &f) ==
            HTTP_CACHE_RESULT_ERROR,
        "NULL url -> ERROR");
  CHECK(browser_fetch_get_with_transport(&ctx, "not-a-url", &resp, 1, fake_fetch,
                                          &f) == HTTP_CACHE_RESULT_ERROR,
        "unparseable url -> ERROR");
  CHECK(browser_fetch_get_with_transport(&ctx, "https://ex.com/", &resp, 1, NULL,
                                          &f) == HTTP_CACHE_RESULT_ERROR,
        "NULL transport -> ERROR");
}

int run_browser_fetch_tests(void) {
  g_failures = 0;
  test_build_url();
  test_fill_request();
  test_multifetch_cache_short_circuit();
  test_cookie_rides_along();
  test_navigation_plan();
  test_hsts_forces_https();
  test_subresource_mixed_content();
  test_page_budget();
  test_fail_closed();
  if (g_failures == 0) printf("[browser_fetch] all multi-fetch runtime tests passed\n");
  return g_failures;
}
