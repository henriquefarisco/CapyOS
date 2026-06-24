/*
 * tests/net/test_http_cookies.c
 *
 * Host-side coverage for the per-domain cookie jar (RFC 6265 subset,
 * src/net/services/http/http_cookies.c). Pure policy + caller-owned jar with an
 * injected clock; drives it by constructing struct http_response Set-Cookie
 * headers and request host/path strings. Locks Set-Cookie parsing, the
 * domain/path match rules, the domain-mismatch REJECT (anti-injection),
 * Max-Age/Expires expiry + deletion, Secure gating, send ordering, eviction and
 * the fail-closed paths. Links http_cache.c (already in the suite) for the
 * shared HTTP-date parser.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "net/http_cookies.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] http_cookies: %s\n", (msg));                             \
    }                                                                          \
  } while (0)

static void resp_reset(struct http_response *r, int status) {
  memset(r, 0, sizeof(*r));
  r->status_code = status;
  r->header_count = 0;
}

static void resp_add(struct http_response *r, const char *name,
                     const char *value) {
  uint32_t i = r->header_count;
  if (i >= HTTP_MAX_HEADERS) return;
  strncpy(r->headers[i].name, name, sizeof(r->headers[i].name) - 1);
  strncpy(r->headers[i].value, value, sizeof(r->headers[i].value) - 1);
  r->header_count = i + 1;
}

/* ---- parse + match units ------------------------------------------------ */

static void test_parse_basic(void) {
  struct http_cookie c;
  CHECK(http_cookie_parse_set_cookie("SID=abc123", "example.com", "/dir/page",
                                     1000, &c) == 1,
        "parse basic ok");
  CHECK(strcmp(c.name, "SID") == 0 && strcmp(c.value, "abc123") == 0,
        "name/value");
  CHECK(c.host_only == 1 && strcmp(c.domain, "example.com") == 0,
        "host-only default domain");
  CHECK(strcmp(c.path, "/dir") == 0, "default-path = /dir");
  CHECK(c.expires == 0 && c.secure == 0, "session, not secure by default");

  /* value may contain '=' (base64 padding). */
  CHECK(http_cookie_parse_set_cookie("t=YWJj==", "ex.com", "/", 1000, &c) == 1 &&
            strcmp(c.value, "YWJj==") == 0,
        "value keeps trailing '='");

  /* no '=' -> malformed. */
  CHECK(http_cookie_parse_set_cookie("justname", "ex.com", "/", 1000, &c) == 0,
        "no '=' rejected");
}

static void test_parse_attrs(void) {
  struct http_cookie c;
  CHECK(http_cookie_parse_set_cookie(
            "SID=abc; Domain=example.com; Path=/app; Secure; HttpOnly; "
            "Max-Age=3600",
            "www.example.com", "/", 1000, &c) == 1,
        "parse full attrs ok");
  CHECK(c.host_only == 0 && strcmp(c.domain, "example.com") == 0,
        "explicit Domain (domain-matched)");
  CHECK(strcmp(c.path, "/app") == 0, "explicit Path");
  CHECK(c.secure == 1 && c.http_only == 1, "Secure + HttpOnly flags");
  CHECK(c.expires == 4600, "Max-Age=3600 -> now+3600");

  /* Max-Age wins over Expires. */
  CHECK(http_cookie_parse_set_cookie(
            "a=b; Max-Age=10; Expires=Sun, 06 Nov 1994 08:49:37 GMT", "ex.com",
            "/", 1000, &c) == 1 &&
            c.expires == 1010,
        "Max-Age beats Expires");

  /* Expires (IMF date) parsed when no Max-Age. */
  CHECK(http_cookie_parse_set_cookie(
            "a=b; Expires=Sun, 06 Nov 1994 08:49:37 GMT", "ex.com", "/", 1000,
            &c) == 1 &&
            c.expires == 784111777L,
        "Expires IMF-date parsed");

  /* leading-dot Domain stripped. */
  CHECK(http_cookie_parse_set_cookie("a=b; Domain=.example.com",
                                     "www.example.com", "/", 1000, &c) == 1 &&
            strcmp(c.domain, "example.com") == 0 && c.host_only == 0,
        "leading-dot Domain stripped");
}

static void test_domain_reject(void) {
  struct http_cookie c;
  /* Domain that does not domain-match the request host is rejected. */
  CHECK(http_cookie_parse_set_cookie("a=b; Domain=evil.com", "example.com", "/",
                                     1000, &c) == 0,
        "cross-domain Set-Cookie rejected");
  CHECK(http_cookie_parse_set_cookie("a=b; Domain=notexample.com",
                                     "example.com", "/", 1000, &c) == 0,
        "suffix-without-dot rejected");
}

static void test_match_rules(void) {
  CHECK(http_cookie_domain_match("example.com", "example.com") == 1, "dom exact");
  CHECK(http_cookie_domain_match("www.example.com", "example.com") == 1,
        "dom subdomain");
  CHECK(http_cookie_domain_match("example.com", "www.example.com") == 0,
        "dom not super");
  CHECK(http_cookie_domain_match("evil.com", "example.com") == 0, "dom diff");
  CHECK(http_cookie_domain_match("1.2.3.4", "1.2.3.4") == 1, "ip exact");

  CHECK(http_cookie_path_match("/app/page", "/app") == 1, "path prefix");
  CHECK(http_cookie_path_match("/app", "/app") == 1, "path exact");
  CHECK(http_cookie_path_match("/application", "/app") == 0,
        "path not a boundary prefix");
  CHECK(http_cookie_path_match("/anything", "/") == 1, "root path matches all");
}

/* ---- jar behaviour via the public API ----------------------------------- */

static void test_jar_send_and_order(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[256];
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "A=1; Path=/");
  resp_add(&r, "Set-Cookie", "B=2; Path=/app");
  CHECK(http_cookie_jar_set_from_response(&j, "example.com", "/app/page", &r,
                                          1000) == 2,
        "two cookies accepted");

  /* both match /app/page; longer path (/app) first. */
  CHECK(http_cookie_jar_header(&j, "example.com", "/app/page", 0, 1000, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "B=2; A=1") == 0,
        "send both, longer path first");

  /* /other -> only the root cookie. */
  CHECK(http_cookie_jar_header(&j, "example.com", "/other", 0, 1000, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "A=1") == 0,
        "path filtering on send");

  /* different domain -> nothing. */
  CHECK(http_cookie_jar_header(&j, "evil.com", "/app/page", 0, 1000, buf,
                               sizeof(buf)) == 0 &&
            buf[0] == '\0',
        "domain filtering on send");
}

static void test_secure_gating(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[128];
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "S=secret; Secure");
  resp_add(&r, "Set-Cookie", "P=plain");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1000);

  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1000, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "P=plain") == 0,
        "insecure request omits Secure cookie");
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 1, 1000, buf,
                               sizeof(buf)) > 0 &&
            (strcmp(buf, "S=secret; P=plain") == 0 ||
             strcmp(buf, "P=plain; S=secret") == 0),
        "secure request includes Secure cookie");
}

static void test_subdomain_send(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[128];
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "D=1; Domain=example.com");
  http_cookie_jar_set_from_response(&j, "www.example.com", "/", &r, 1000);
  /* domain cookie is sent to the subdomain AND the apex. */
  CHECK(http_cookie_jar_header(&j, "www.example.com", "/", 0, 1000, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "D=1") == 0,
        "domain cookie sent to subdomain");
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1000, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "D=1") == 0,
        "domain cookie sent to apex");
  /* host-only would NOT do this -- check a host-only cookie. */
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "H=1"); /* host-only (no Domain) */
  http_cookie_jar_set_from_response(&j, "www.example.com", "/", &r, 1000);
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1000, buf,
                               sizeof(buf)) == 0,
        "host-only cookie not sent to a different host");
}

static void test_expiry_and_delete(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[128];
  http_cookie_jar_init(&j);

  /* store a cookie that expires at t=1100. */
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "F=6; Max-Age=100");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1000);
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1050, buf,
                               sizeof(buf)) > 0,
        "cookie sent while fresh");
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 2000, buf,
                               sizeof(buf)) == 0,
        "expired cookie not sent");

  /* Max-Age=0 deletes a stored cookie. */
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "G=7");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1000);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "G=7; Max-Age=0");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1010);
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1010, buf,
                               sizeof(buf)) == 0,
        "Max-Age=0 deletes the cookie");
  CHECK(j.deleted >= 1, "delete stat");
}

static void test_update_and_eviction(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[64];
  char setc[48];
  unsigned k;
  http_cookie_jar_init(&j);

  /* same name/domain/path updates value, not a new entry. */
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "U=old");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1000);
  resp_reset(&r, 200);
  resp_add(&r, "Set-Cookie", "U=new");
  http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1001);
  CHECK(http_cookie_jar_header(&j, "example.com", "/", 0, 1001, buf,
                               sizeof(buf)) > 0 &&
            strcmp(buf, "U=new") == 0,
        "re-set updates value");

  /* overflow the jar; eviction keeps it bounded. */
  http_cookie_jar_init(&j);
  for (k = 0; k < HTTP_COOKIE_MAX + 4; k++) {
    snprintf(setc, sizeof(setc), "c%u=v; Path=/p%u", k, k);
    resp_reset(&r, 200);
    resp_add(&r, "Set-Cookie", setc);
    http_cookie_jar_set_from_response(&j, "example.com", "/", &r, 1000 + k);
  }
  CHECK(j.evictions == 4, "exactly 4 evictions at capacity+4");
}

static void test_fail_closed(void) {
  static struct http_cookie_jar j;
  struct http_response r;
  char buf[16];
  struct http_cookie c;
  http_cookie_jar_init(&j);
  resp_reset(&r, 200);
  CHECK(http_cookie_jar_set_from_response(NULL, "e", "/", &r, 1) == 0,
        "NULL jar set fails");
  CHECK(http_cookie_jar_header(NULL, "e", "/", 0, 1, buf, sizeof(buf)) == 0,
        "NULL jar header fails");
  CHECK(http_cookie_parse_set_cookie(NULL, "e", "/", 1, &c) == 0,
        "NULL set-cookie fails");
  CHECK(http_cookie_jar_header(&j, "e", "/", 0, 1, buf, 0) == 0,
        "zero cap fails closed");
}

int run_http_cookies_tests(void) {
  g_failures = 0;
  test_parse_basic();
  test_parse_attrs();
  test_domain_reject();
  test_match_rules();
  test_jar_send_and_order();
  test_secure_gating();
  test_subdomain_send();
  test_expiry_and_delete();
  test_update_and_eviction();
  test_fail_closed();
  if (g_failures == 0) printf("[http_cookies] all cookie tests passed\n");
  return g_failures;
}
