/*
 * tests/net/test_http_session.c
 *
 * Host-side coverage for the HTTP fetch session (src/net/services/http/
 * http_session.c) that composes the response cache + cookie jar around an
 * injected transport. Drives it with a fake fetch that records the inbound
 * Cookie header and returns canned status/Set-Cookie/body, so the whole flow is
 * deterministic and network-free. Links http_session.c (this test) +
 * http_cache.c + http_cookies.c (already in the suite).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "net/http_session.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] http_session: %s\n", (msg));                             \
    }                                                                          \
  } while (0)

static void resp_reset(struct http_response *r, int status) {
  memset(r, 0, sizeof(*r));
  r->status_code = status;
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
}

struct fake {
  int calls;
  char seen_cookie[256]; /* Cookie header value the last fetch observed */
  int status;
  const char *set_cookie; /* Set-Cookie to return (optional) */
  const char *cc;         /* Cache-Control to return (optional) */
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
  resp_reset(resp, f->status);
  if (f->cc) resp_add(resp, "Cache-Control", f->cc);
  if (f->set_cookie) resp_add(resp, "Set-Cookie", f->set_cookie);
  resp->body = (uint8_t *)f->body;
  resp->body_len = f->body_len;
  return 0;
}

static void test_cache_plus_cookie_flow(void) {
  static struct http_session s;
  struct http_request q;
  struct http_response resp;
  struct fake f;
  static const uint8_t body[3] = {1, 2, 3};
  http_session_init(&s);

  /* 1. MISS /login: fetched + Set-Cookie stored + body cached (max-age=100). */
  f.calls = 0;
  f.status = 200;
  f.cc = "max-age=100";
  f.set_cookie = "SID=xyz";
  f.body = body;
  f.body_len = 3;
  get_req(&q, "ex.com", "/login", 1);
  CHECK(http_session_fetch(&s, &q, &resp, 1000, fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED &&
            f.calls == 1,
        "miss -> fetched");

  /* 2. 2nd /login within max-age -> served from cache, NO fetch. */
  get_req(&q, "ex.com", "/login", 1);
  CHECK(http_session_fetch(&s, &q, &resp, 1050, fake_fetch, &f) ==
                HTTP_CACHE_RESULT_FRESH_SERVED &&
            f.calls == 1,
        "2nd /login served from cache, no refetch");

  /* 3. /app (uncached) -> fetched, and the request carries the stored cookie. */
  f.set_cookie = NULL;
  f.cc = NULL;
  f.body = NULL;
  f.body_len = 0;
  get_req(&q, "ex.com", "/app", 1);
  CHECK(http_session_fetch(&s, &q, &resp, 1100, fake_fetch, &f) ==
                HTTP_CACHE_RESULT_MISS_FETCHED &&
            f.calls == 2,
        "/app fetched (uncacheable)");
  CHECK(strcmp(f.seen_cookie, "SID=xyz") == 0,
        "request carried the stored cookie");
  CHECK(s.cookie_headers_attached >= 1, "cookie-attached stat");
}

static void test_secure_cookie_gating(void) {
  static struct http_session s;
  struct http_request q;
  struct http_response resp;
  struct fake f;
  http_session_init(&s);

  /* set a Secure cookie over https. */
  f.calls = 0;
  f.status = 200;
  f.cc = NULL;
  f.set_cookie = "T=1; Secure";
  f.body = NULL;
  f.body_len = 0;
  get_req(&q, "ex.com", "/", 1);
  http_session_fetch(&s, &q, &resp, 1000, fake_fetch, &f);

  /* an http request must NOT carry the Secure cookie. */
  f.set_cookie = NULL;
  get_req(&q, "ex.com", "/", 0);
  http_session_fetch(&s, &q, &resp, 1001, fake_fetch, &f);
  CHECK(f.seen_cookie[0] == '\0', "Secure cookie omitted over http");

  /* an https request carries it. */
  get_req(&q, "ex.com", "/", 1);
  http_session_fetch(&s, &q, &resp, 1002, fake_fetch, &f);
  CHECK(strcmp(f.seen_cookie, "T=1") == 0, "Secure cookie sent over https");
}

static void test_fail_closed(void) {
  static struct http_session s;
  struct http_request q;
  struct http_response resp;
  struct fake f;
  http_session_init(&s);
  get_req(&q, "ex.com", "/", 1);
  CHECK(http_session_fetch(NULL, &q, &resp, 1, fake_fetch, &f) ==
            HTTP_CACHE_RESULT_ERROR,
        "NULL session -> ERROR");
  CHECK(http_session_fetch(&s, &q, &resp, 1, NULL, &f) ==
            HTTP_CACHE_RESULT_ERROR,
        "NULL fetch fn -> ERROR");
  CHECK(http_session_attach_cookies(NULL, &q, 1) == 0, "NULL attach -> 0");
}

int run_http_session_tests(void) {
  g_failures = 0;
  test_cache_plus_cookie_flow();
  test_secure_cookie_gating();
  test_fail_closed();
  if (g_failures == 0) printf("[http_session] all session tests passed\n");
  return g_failures;
}
